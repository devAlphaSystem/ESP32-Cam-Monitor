#include "persistent_log.h"

#include <FS.h>
#include <LittleFS.h>
#include <stddef.h>
#include <time.h>

namespace PersistentLog {
namespace {

constexpr char PARTITION_LABEL[] = "logs";
constexpr char MOUNT_PATH[] = "/eventlogs";
constexpr char LOG_FILE_PATH[] = "/events.bin";
constexpr uint32_t RECORD_MAGIC = 0x434C4F47UL;
constexpr uint16_t LEGACY_RECORD_SCHEMA_VERSION = 1;
constexpr uint16_t RECORD_SCHEMA_VERSION = 2;
constexpr size_t STORED_CONTEXT_BYTES = 4;
constexpr size_t STORED_DETAIL_CAPACITY = DETAIL_CAPACITY - STORED_CONTEXT_BYTES;
constexpr size_t WRITE_QUEUE_CAPACITY = 16;
constexpr unsigned long FLUSH_INTERVAL_MS = 1000UL;

struct StoredRecord {
    uint32_t magic;
    uint32_t sequence;
    uint32_t epochSeconds;
    uint32_t uptimeMs;
    uint32_t targetIp;
    uint32_t durationMs;
    uint32_t value;
    uint16_t targetPort;
    int16_t statusCode;
    uint16_t event;
    uint16_t schemaVersion;
    uint8_t category;
    uint8_t level;
    uint8_t outcome;
    uint8_t flags;
    uint8_t payload[DETAIL_CAPACITY];
    uint32_t checksum;
};

static_assert(DETAIL_CAPACITY > STORED_CONTEXT_BYTES + 1, "O payload persistente precisa reservar espaco para o detalhe");
static_assert(sizeof(StoredRecord) == 80, "O registro persistente deve ocupar exatamente 80 bytes");
static_assert(CAPACITY * sizeof(StoredRecord) == 800000, "O arquivo circular deve permanecer dentro da particao de logs");

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

uint32_t checksumBytes(const void* values, size_t valueSize)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(values);
    uint32_t hash = 2166136261UL;

    for (size_t index = 0; index < valueSize; index++) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }

    return hash;
}

uint32_t recordChecksum(const StoredRecord& record)
{
    return checksumBytes(&record, offsetof(StoredRecord, checksum));
}

bool validRecord(const StoredRecord& record)
{
    return record.magic == RECORD_MAGIC && (record.schemaVersion == LEGACY_RECORD_SCHEMA_VERSION || record.schemaVersion == RECORD_SCHEMA_VERSION) && record.checksum == recordChecksum(record);
}

void writeUint16(uint8_t* destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>(value & 0xFFU);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

uint16_t readUint16(const uint8_t* source)
{
    return static_cast<uint16_t>(source[0]) | (static_cast<uint16_t>(source[1]) << 8);
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
        case Event::SntpSync:
        case Event::RuntimeContext:
        case Event::LogsCleared:
            return Category::System;
        case Event::WifiConnect:
        case Event::WifiDisconnect:
        case Event::WifiReconnect:
            return Category::Wifi;
        case Event::CameraState:
        case Event::Incident:
        case Event::CameraReboot:
        case Event::PtzCommand:
        case Event::CameraTargetLoad:
        case Event::CameraTargetSave:
            return Category::Camera;
        case Event::DiscoveryProbe:
        case Event::DiscoveryResponse:
        case Event::DiscoveryResult:
        case Event::SubnetScan:
        case Event::TcpScan:
            return Category::Discovery;
        case Event::OnvifProbe:
        case Event::OnvifSoap:
            return Category::Onvif;
        case Event::SnapshotHttp:
        case Event::SnapshotTransfer:
            return Category::Snapshot;
        case Event::RtspProbe:
            return Category::Rtsp;
        case Event::HealthCheck:
            return Category::Health;
        case Event::AdminAction:
            return Category::Action;
        case Event::EmailHttp:
            return Category::Notification;
        case Event::SettingsSaved:
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

    if (stored.schemaVersion == LEGACY_RECORD_SCHEMA_VERSION) {
        memcpy(entry.detail, stored.payload, DETAIL_CAPACITY);
    } else {
        entry.bootId = readUint16(stored.payload);
        entry.contextId = readUint16(stored.payload + 2);
        memcpy(entry.detail, stored.payload + STORED_CONTEXT_BYTES, STORED_DETAIL_CAPACITY);
    }

    entry.detail[DETAIL_CAPACITY - 1] = '\0';
}

}

bool begin()
{
    if (storeReady) {
        return true;
    }

    if (!LittleFS.begin(true, MOUNT_PATH, 3, PARTITION_LABEL)) {
        return false;
    }

    if (!openLogFile()) {
        LittleFS.end();
        return false;
    }

    storeReady = true;
    scanExistingRecords();
    currentBootId = static_cast<uint16_t>(nextSequence & 0xFFFFU);

    if (currentBootId == 0) {
        currentBootId = 1;
    }

    lastFlushAtMs = millis();
    return true;
}

bool ready()
{
    return storeReady;
}

void record(Event event, Outcome outcome, const char* detail, const IPAddress& target, uint16_t targetPort, int statusCode, uint32_t durationMs, uint32_t value, uint8_t flags, Level level, bool critical, uint16_t contextId)
{
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

    if (level == Level::Automatic) {
        level = outcome == Outcome::Failure ? Level::Error : outcome == Outcome::Skipped ? Level::Warning : Level::Info;
    }

    stored.level = static_cast<uint8_t>(level);
    stored.outcome = static_cast<uint8_t>(outcome);
    stored.flags = flags;
    writeUint16(stored.payload, currentBootId);
    writeUint16(stored.payload + 2, contextId);
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
    return queuedRecords == 0;
}

bool clear()
{
    if (!storeReady) {
        return false;
    }

    logFile.close();
    fs::File clearedFile = LittleFS.open(LOG_FILE_PATH, FILE_WRITE);

    if (!clearedFile) {
        writeErrors++;

        if (!openLogFile()) {
            storeReady = false;
        }

        return false;
    }

    clearedFile.close();
    queuedRecords = 0;
    persistedRecords = 0;
    nextSlot = 0;
    newestSlot = 0;
    nextSequence = 1;
    droppedRecords = 0;
    writeErrors = 0;
    lastFlushAtMs = millis();

    if (!openLogFile()) {
        storeReady = false;
        writeErrors = 1;
        return false;
    }

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
