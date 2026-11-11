#include "persistent_log_format.h"

#include <FS.h>
#include <LittleFS.h>
#include <esp_attr.h>
#include <time.h>

namespace PersistentLog {
namespace {

constexpr char PARTITION_LABEL[] = "logs";
constexpr char MOUNT_PATH[] = "/eventlogs";
constexpr char LOG_FILE_PATH[] = "/events.bin";
constexpr size_t WRITE_QUEUE_CAPACITY = 16;
constexpr unsigned long FLUSH_INTERVAL_MS = 1000UL;
constexpr uint8_t STORAGE_RECOVERY_USAGE_PERCENT = 75;
constexpr uint32_t STORAGE_OPERATION_MAGIC = 0x4C4F4757UL;

using PersistentLogFormat::RECORD_MAGIC;
using PersistentLogFormat::RECORD_SCHEMA_VERSION;
using PersistentLogFormat::STORED_CONTEXT_BYTES;
using PersistentLogFormat::STORED_DETAIL_CAPACITY;
using PersistentLogFormat::StoredRecord;
using PersistentLogFormat::readUint16;
using PersistentLogFormat::recordChecksum;
using PersistentLogFormat::validRecord;

struct StorageOperationMarker {
    uint32_t magic;
    uint32_t invertedMagic;
};

RTC_NOINIT_ATTR volatile StorageOperationMarker storageOperationMarker;

fs::File logFile;
StoredRecord writeQueue[WRITE_QUEUE_CAPACITY];
size_t queuedRecords = 0;
size_t persistedRecords = 0;
size_t nextSlot = 0;
size_t newestSlot = 0;
uint32_t nextSequence = 1;
uint32_t droppedRecords = 0;
uint32_t writeErrors = 0;
unsigned long lastFlushAtMs = 0;
uint16_t currentBootId = 0;
bool storeReady = false;

bool storageOperationWasInterrupted()
{
    return storageOperationMarker.magic == STORAGE_OPERATION_MAGIC && storageOperationMarker.invertedMagic == ~STORAGE_OPERATION_MAGIC;
}

void markStorageOperationActive()
{
    storageOperationMarker.magic = 0;
    storageOperationMarker.invertedMagic = ~STORAGE_OPERATION_MAGIC;
    storageOperationMarker.magic = STORAGE_OPERATION_MAGIC;
}

void clearStorageOperationMarker()
{
    storageOperationMarker.magic = 0;
    storageOperationMarker.invertedMagic = 0;
}

void writeUint16(uint8_t* destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>(value & 0xFFU);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

uint32_t ipToValue(const IPAddress& ip)
{
    return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) | (static_cast<uint32_t>(ip[2]) << 8) | static_cast<uint32_t>(ip[3]);
}

Category categoryForEvent(Event event)
{
    switch (event) {
        case Event::SystemBoot:
        case Event::SystemRestart:
        case Event::LogsCleared:
            return Category::System;
        case Event::WifiConnect:
        case Event::WifiDisconnect:
            return Category::Wifi;
        case Event::CameraState:
        case Event::Incident:
        case Event::CameraReboot:
        case Event::PtzCommand:
            return Category::Camera;
        case Event::DiscoveryProbe:
        case Event::DiscoveryResponse:
        case Event::DiscoveryResult:
            return Category::Discovery;
        case Event::OnvifProbe:
        case Event::OnvifSoap:
            return Category::Onvif;
        case Event::SnapshotHttp:
        case Event::SnapshotTransfer:
            return Category::Snapshot;
        case Event::RtspProbe:
        case Event::LiveSession:
            return Category::Rtsp;
        case Event::HealthCheck:
            return Category::Health;
        case Event::AdminAction:
            return Category::Action;
        case Event::EmailHttp:
            return Category::Notification;
        case Event::SettingsReset:
        case Event::SettingsContext:
            return Category::Settings;
    }

    return Category::System;
}

void copyDetail(char* destination, size_t capacity, const char* source)
{
    memset(destination, 0, capacity);

    if (source == nullptr || capacity == 0) {
        return;
    }

    const size_t copyLength = min(strlen(source), capacity - 1);
    memcpy(destination, source, copyLength);
    destination[copyLength] = '\0';
}

bool readStoredRecord(size_t slot, StoredRecord& record)
{
    if (!storeReady || slot >= CAPACITY) {
        return false;
    }

    const size_t offset = slot * sizeof(StoredRecord);

    if (!logFile.seek(offset, SeekSet)) {
        return false;
    }

    return logFile.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) && validRecord(record);
}

void scanExistingRecords()
{
    persistedRecords = 0;
    nextSlot = 0;
    newestSlot = 0;
    nextSequence = 1;

    const size_t fileSlots = min(static_cast<size_t>((logFile.size() + sizeof(StoredRecord) - 1) / sizeof(StoredRecord)), CAPACITY);
    bool foundRecord = false;
    uint32_t newestSequence = 0;

    if (!logFile.seek(0, SeekSet)) {
        return;
    }

    for (size_t slot = 0; slot < fileSlots; slot++) {
        StoredRecord record{};
        const bool recordValid = logFile.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) && validRecord(record);

        if ((slot & 0xFFU) == 0) {
            delay(0);
        }

        if (!recordValid) {
            continue;
        }

        persistedRecords++;

        if (!foundRecord || record.sequence > newestSequence) {
            foundRecord = true;
            newestSequence = record.sequence;
            newestSlot = slot;
        }
    }

    if (!foundRecord) {
        return;
    }

    nextSequence = newestSequence == UINT32_MAX ? 1 : newestSequence + 1;
    nextSlot = (newestSlot + 1) % CAPACITY;
    persistedRecords = min(persistedRecords, CAPACITY);
}

bool openLogFile()
{
    if (!LittleFS.exists(LOG_FILE_PATH)) {
        fs::File created = LittleFS.open(LOG_FILE_PATH, FILE_WRITE);

        if (!created) {
            return false;
        }

        created.close();
    }

    logFile = LittleFS.open(LOG_FILE_PATH, "r+");
    return static_cast<bool>(logFile);
}

bool mountLogStorage(bool forceFormat, bool& formatted)
{
    formatted = false;
    const bool mounted = LittleFS.begin(false, MOUNT_PATH, 3, PARTITION_LABEL);

    if (mounted && !forceFormat) {
        return true;
    }

    if (mounted) {
        LittleFS.end();
    }

    markStorageOperationActive();

    if (!LittleFS.format() || !LittleFS.begin(false, MOUNT_PATH, 3, PARTITION_LABEL)) {
        return false;
    }

    formatted = true;
    return true;
}

bool storageNeedsRecovery()
{
    const size_t totalBytes = LittleFS.totalBytes();
    const size_t usedBytes = LittleFS.usedBytes();
    const size_t maximumFileBytes = CAPACITY * sizeof(StoredRecord);

    if (totalBytes == 0 || usedBytes > totalBytes || logFile.size() > maximumFileBytes) {
        return true;
    }

    return static_cast<uint64_t>(usedBytes) * 100ULL > static_cast<uint64_t>(totalBytes) * STORAGE_RECOVERY_USAGE_PERCENT;
}

bool reformatLogStorage()
{
    logFile.close();
    LittleFS.end();
    markStorageOperationActive();

    if (!LittleFS.format() || !LittleFS.begin(false, MOUNT_PATH, 3, PARTITION_LABEL)) {
        return false;
    }

    return openLogFile();
}

void decodeRecord(const StoredRecord& stored, Entry& entry)
{
    entry = Entry{};
    entry.sequence = stored.sequence;
    entry.epochSeconds = stored.epochSeconds;
    entry.uptimeMs = stored.uptimeMs;
    entry.targetIp = stored.targetIp;
    entry.durationMs = stored.durationMs;
    entry.value = stored.value;
    entry.targetPort = stored.targetPort;
    entry.statusCode = stored.statusCode;
    entry.event = static_cast<Event>(stored.event);
    entry.category = static_cast<Category>(stored.category);
    entry.level = static_cast<Level>(stored.level);
    entry.outcome = static_cast<Outcome>(stored.outcome);
    entry.flags = stored.flags;
    entry.bootId = readUint16(stored.payload);
    entry.contextId = readUint16(stored.payload + 2);
    entry.cameraId = PersistentLogFormat::readUint32(stored.payload + 4);
    memcpy(entry.detail, stored.payload + STORED_CONTEXT_BYTES, STORED_DETAIL_CAPACITY);
    entry.detail[DETAIL_CAPACITY - 1] = '\0';
}

}

bool begin()
{
    if (storeReady) {
        return true;
    }

    bool storageFormatted = false;

    if (!mountLogStorage(storageOperationWasInterrupted(), storageFormatted)) {
        return false;
    }

    markStorageOperationActive();

    if (!openLogFile()) {
        LittleFS.end();
        return false;
    }

    if (storageNeedsRecovery()) {
        if (!reformatLogStorage()) {
            LittleFS.end();
            return false;
        }

        storageFormatted = true;
    }

    storeReady = true;
    scanExistingRecords();
    currentBootId = static_cast<uint16_t>(nextSequence & 0xFFFFU);

    if (currentBootId == 0) {
        currentBootId = 1;
    }

    lastFlushAtMs = millis();
    clearStorageOperationMarker();

    if (storageFormatted) {
        Serial.println("Logs partition reinitialized to recover the storage.");
    }

    return true;
}

bool ready()
{
    return storeReady;
}

void record(Event event, Outcome outcome, const char* detail, const IPAddress& target, uint16_t targetPort, int statusCode, uint32_t durationMs, uint32_t value, uint8_t flags, Level level, bool critical, uint16_t contextId, uint32_t cameraId)
{
    if (level == Level::Automatic) {
        level = outcome == Outcome::Failure ? Level::Error : outcome == Outcome::Skipped ? Level::Warning : Level::Info;
    }
    if (level != Level::Warning && level != Level::Error)
        return;

    if (!storeReady) {
        droppedRecords++;
        return;
    }

    if (queuedRecords >= WRITE_QUEUE_CAPACITY && !flush()) {
        droppedRecords++;
        return;
    }

    StoredRecord& stored = writeQueue[queuedRecords++];
    stored = StoredRecord{};
    stored.magic = RECORD_MAGIC;
    stored.sequence = nextSequence;
    nextSequence = nextSequence == UINT32_MAX ? 1 : nextSequence + 1;

    const time_t now = time(nullptr);
    stored.epochSeconds = now >= 1700000000 ? static_cast<uint32_t>(now) : 0;
    stored.uptimeMs = millis();
    stored.targetIp = ipToValue(target);
    stored.durationMs = durationMs;
    stored.value = value;
    stored.targetPort = targetPort;
    stored.statusCode = static_cast<int16_t>(constrain(statusCode, static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));
    stored.event = static_cast<uint16_t>(event);
    stored.schemaVersion = RECORD_SCHEMA_VERSION;
    stored.category = static_cast<uint8_t>(categoryForEvent(event));

    stored.level = static_cast<uint8_t>(level);
    stored.outcome = static_cast<uint8_t>(outcome);
    stored.flags = flags;
    writeUint16(stored.payload, currentBootId);
    writeUint16(stored.payload + 2, contextId);
    for (uint8_t index = 0; index < 4; index++)
        stored.payload[4 + index] = static_cast<uint8_t>(cameraId >> (index * 8));
    copyDetail(reinterpret_cast<char*>(stored.payload + STORED_CONTEXT_BYTES), STORED_DETAIL_CAPACITY, detail);
    stored.checksum = recordChecksum(stored);

    if (critical || queuedRecords >= WRITE_QUEUE_CAPACITY) {
        flush();
    }
}

void maintain()
{
    if (queuedRecords > 0 && millis() - lastFlushAtMs >= FLUSH_INTERVAL_MS) {
        flush();
    }
}

bool flush()
{
    if (!storeReady || queuedRecords == 0) {
        return storeReady;
    }

    markStorageOperationActive();
    size_t writtenRecords = 0;

    while (writtenRecords < queuedRecords) {
        const size_t offset = nextSlot * sizeof(StoredRecord);

        if (!logFile.seek(offset, SeekSet) || logFile.write(reinterpret_cast<const uint8_t*>(&writeQueue[writtenRecords]), sizeof(StoredRecord)) != sizeof(StoredRecord)) {
            writeErrors++;
            break;
        }

        newestSlot = nextSlot;
        nextSlot = (nextSlot + 1) % CAPACITY;

        if (persistedRecords < CAPACITY) {
            persistedRecords++;
        }

        writtenRecords++;
    }

    if (writtenRecords > 0) {
        logFile.flush();
        lastFlushAtMs = millis();
    }

    if (writtenRecords < queuedRecords) {
        memmove(writeQueue, writeQueue + writtenRecords, (queuedRecords - writtenRecords) * sizeof(StoredRecord));
    }

    queuedRecords -= writtenRecords;

    if (queuedRecords == 0) {
        clearStorageOperationMarker();
    }

    return queuedRecords == 0;
}

bool clear()
{
    if (!storeReady) {
        return false;
    }

    storeReady = false;

    if (!reformatLogStorage()) {
        writeErrors++;
        return false;
    }

    storeReady = true;
    queuedRecords = 0;
    persistedRecords = 0;
    nextSlot = 0;
    newestSlot = 0;
    nextSequence = 1;
    droppedRecords = 0;
    writeErrors = 0;
    lastFlushAtMs = millis();
    clearStorageOperationMarker();

    return true;
}

bool readNewest(size_t offset, Entry& entry)
{
    if (!storeReady) {
        return false;
    }

    flush();

    if (offset >= persistedRecords) {
        return false;
    }

    const size_t slot = (newestSlot + CAPACITY - (offset % CAPACITY)) % CAPACITY;
    StoredRecord stored{};

    if (readStoredRecord(slot, stored)) {
        decodeRecord(stored, entry);
        return true;
    }

    return false;
}

size_t count()
{
    return min(persistedRecords + queuedRecords, CAPACITY);
}

size_t capacity()
{
    return CAPACITY;
}

size_t pendingCount()
{
    return queuedRecords;
}

size_t storageTotalBytes()
{
    return storeReady ? LittleFS.totalBytes() : 0;
}

size_t storageUsedBytes()
{
    return storeReady ? LittleFS.usedBytes() : 0;
}

size_t storageFileBytes()
{
    return storeReady ? logFile.size() : 0;
}

uint32_t droppedCount()
{
    return droppedRecords;
}

uint32_t writeErrorCount()
{
    return writeErrors;
}

}
