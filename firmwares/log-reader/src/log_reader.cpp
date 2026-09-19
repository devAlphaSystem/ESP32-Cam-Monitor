#include "log_reader.h"

#include <esp_attr.h>
#include <esp_littlefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace LogReader {
namespace {

constexpr char PARTITION_LABEL[] = "logs";
constexpr char MOUNT_PATH[] = "/eventlogs";
constexpr char LOG_FILE_PATH[] = "/eventlogs/events.bin";
constexpr uint32_t MOUNT_OPERATION_MAGIC = 0x4C524D54UL;

struct MountOperationMarker {
    uint32_t magic;
    uint32_t invertedMagic;
};

struct __attribute__((packed)) RecordIndex {
    uint32_t sequence;
    uint16_t slot;
};

static_assert(sizeof(RecordIndex) == 6, "O indice deve preservar heap");
static_assert(sizeof(PersistentLogFormat::StoredRecord) == 80, "Formato persistente inesperado");

RTC_NOINIT_ATTR volatile MountOperationMarker mountOperationMarker;

Status currentStatus;
const esp_partition_t* logsPartition = nullptr;
FILE* logFile = nullptr;
RecordIndex* recordIndex = nullptr;

bool mountWasInterrupted()
{
    return mountOperationMarker.magic == MOUNT_OPERATION_MAGIC && mountOperationMarker.invertedMagic == ~MOUNT_OPERATION_MAGIC;
}

void markMountActive()
{
    mountOperationMarker.magic = 0;
    mountOperationMarker.invertedMagic = ~MOUNT_OPERATION_MAGIC;
    mountOperationMarker.magic = MOUNT_OPERATION_MAGIC;
}

void clearMountMarker()
{
    mountOperationMarker.magic = 0;
    mountOperationMarker.invertedMagic = 0;
}

int compareNewestFirst(const void* leftValue, const void* rightValue)
{
    const RecordIndex& left = *static_cast<const RecordIndex*>(leftValue);
    const RecordIndex& right = *static_cast<const RecordIndex*>(rightValue);

    if (left.sequence == right.sequence) {
        return left.slot < right.slot ? 1 : (left.slot > right.slot ? -1 : 0);
    }

    return left.sequence < right.sequence ? 1 : -1;
}

bool readStoredRecord(uint16_t slot, PersistentLogFormat::StoredRecord& record)
{
    if (logFile == nullptr || slot >= currentStatus.slotCount) {
        return false;
    }

    const long offset = static_cast<long>(slot) * static_cast<long>(sizeof(record));
    return fseek(logFile, offset, SEEK_SET) == 0 && fread(&record, 1, sizeof(record), logFile) == sizeof(record);
}

bool recordHasKnownSchema(const PersistentLogFormat::StoredRecord& record)
{
    return record.schemaVersion == PersistentLogFormat::LEGACY_RECORD_SCHEMA_VERSION || record.schemaVersion == PersistentLogFormat::CONTEXT_RECORD_SCHEMA_VERSION || record.schemaVersion == PersistentLogFormat::RECORD_SCHEMA_VERSION;
}

void scanRecords()
{
    if (logFile == nullptr || currentStatus.slotCount == 0) {
        return;
    }

    recordIndex = static_cast<RecordIndex*>(malloc(currentStatus.slotCount * sizeof(RecordIndex)));

    if (recordIndex == nullptr) {
        currentStatus.mountError = ESP_ERR_NO_MEM;
        fclose(logFile);
        logFile = nullptr;
        currentStatus.fileFound = false;
        return;
    }

    for (size_t slot = 0; slot < currentStatus.slotCount; slot++) {
        PersistentLogFormat::StoredRecord record{};

        if (!readStoredRecord(static_cast<uint16_t>(slot), record)) {
            currentStatus.invalidCount++;
            clearerr(logFile);
            continue;
        }

        if (record.magic == PersistentLogFormat::RECORD_MAGIC && record.checksum == PersistentLogFormat::recordChecksum(record) && !recordHasKnownSchema(record)) {
            currentStatus.unknownSchemaCount++;
            continue;
        }

        if (!PersistentLogFormat::validRecord(record)) {
            currentStatus.invalidCount++;
            continue;
        }

        recordIndex[currentStatus.validCount].sequence = record.sequence;
        recordIndex[currentStatus.validCount].slot = static_cast<uint16_t>(slot);
        currentStatus.validCount++;
    }

    if (currentStatus.validCount > 1) {
        qsort(recordIndex, currentStatus.validCount, sizeof(RecordIndex), compareNewestFirst);
    }
}

void copyDetail(const PersistentLogFormat::StoredRecord& record, Entry& entry)
{
    const uint8_t* source = record.payload;
    size_t sourceSize = PersistentLog::DETAIL_CAPACITY;

    if (record.schemaVersion != PersistentLogFormat::LEGACY_RECORD_SCHEMA_VERSION) {
        entry.bootId = PersistentLogFormat::readUint16(record.payload);
        entry.contextId = PersistentLogFormat::readUint16(record.payload + 2);
        const size_t contextBytes = record.schemaVersion == PersistentLogFormat::CONTEXT_RECORD_SCHEMA_VERSION ? 4 : PersistentLogFormat::STORED_CONTEXT_BYTES;
        if (record.schemaVersion == PersistentLogFormat::RECORD_SCHEMA_VERSION)
            entry.cameraId = PersistentLogFormat::readUint32(record.payload + 4);
        source += contextBytes;
        sourceSize -= contextBytes;
    }

    const void* terminator = memchr(source, '\0', sourceSize);
    const size_t copySize = terminator == nullptr ? sourceSize : static_cast<const uint8_t*>(terminator) - source;
    memcpy(entry.detail, source, copySize);
    entry.detail[copySize] = '\0';
}

}

bool begin()
{
    logsPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION_LABEL);

    if (logsPartition != nullptr) {
        currentStatus.partitionAddress = logsPartition->address;
        currentStatus.partitionSize = logsPartition->size;
    }

    if (mountWasInterrupted()) {
        currentStatus.mountSkippedAfterReset = true;
        currentStatus.mountError = ESP_ERR_INVALID_STATE;
        clearMountMarker();
        return false;
    }

    currentStatus.mountAttempted = true;
    markMountActive();

    const esp_vfs_littlefs_conf_t configuration{
        .base_path = MOUNT_PATH,
        .partition_label = PARTITION_LABEL,
        .partition = nullptr,
        .format_if_mount_failed = false,
        .read_only = true,
        .dont_mount = false,
        .grow_on_mount = false,
    };

    currentStatus.mountError = esp_vfs_littlefs_register(&configuration);
    clearMountMarker();

    if (currentStatus.mountError != ESP_OK) {
        return false;
    }

    currentStatus.mounted = true;
    logFile = fopen(LOG_FILE_PATH, "rb");

    if (logFile == nullptr) {
        return true;
    }

    currentStatus.fileFound = true;

    if (fseek(logFile, 0, SEEK_END) != 0) {
        currentStatus.mountError = ESP_FAIL;
        fclose(logFile);
        logFile = nullptr;
        currentStatus.fileFound = false;
        return false;
    }

    const long measuredSize = ftell(logFile);

    if (measuredSize < 0) {
        currentStatus.mountError = ESP_FAIL;
        fclose(logFile);
        logFile = nullptr;
        currentStatus.fileFound = false;
        return false;
    }

    currentStatus.fileSize = static_cast<uint32_t>(measuredSize);
    const size_t physicalSlotLimit = currentStatus.partitionSize / sizeof(PersistentLogFormat::StoredRecord);
    currentStatus.slotCount = min(static_cast<size_t>(currentStatus.fileSize / sizeof(PersistentLogFormat::StoredRecord)), physicalSlotLimit);
    currentStatus.partialBytes = static_cast<uint16_t>(currentStatus.fileSize % sizeof(PersistentLogFormat::StoredRecord));
    rewind(logFile);
    scanRecords();
    return currentStatus.fileFound;
}

const Status& status()
{
    return currentStatus;
}

const esp_partition_t* partition()
{
    return logsPartition;
}

bool readNewest(size_t index, Entry& entry)
{
    if (recordIndex == nullptr || index >= currentStatus.validCount) {
        return false;
    }

    PersistentLogFormat::StoredRecord record{};

    if (!readStoredRecord(recordIndex[index].slot, record) || !PersistentLogFormat::validRecord(record)) {
        return false;
    }

    entry = Entry{};
    entry.slot = recordIndex[index].slot;
    entry.schemaVersion = record.schemaVersion;
    entry.sequence = record.sequence;
    entry.epochSeconds = record.epochSeconds;
    entry.uptimeMs = record.uptimeMs;
    entry.targetIp = record.targetIp;
    entry.durationMs = record.durationMs;
    entry.value = record.value;
    entry.targetPort = record.targetPort;
    entry.statusCode = record.statusCode;
    entry.event = record.event;
    entry.category = record.category;
    entry.level = record.level;
    entry.outcome = record.outcome;
    entry.flags = record.flags;
    copyDetail(record, entry);
    return true;
}

const char* levelName(uint8_t level)
{
    switch (static_cast<PersistentLog::Level>(level)) {
        case PersistentLog::Level::Info:
            return "info";
        case PersistentLog::Level::Warning:
            return "warning";
        case PersistentLog::Level::Error:
            return "error";
        case PersistentLog::Level::Automatic:
            return "unknown";
    }

    return "unknown";
}

const char* categoryName(uint8_t category)
{
    switch (static_cast<PersistentLog::Category>(category)) {
        case PersistentLog::Category::System:
            return "system";
        case PersistentLog::Category::Wifi:
            return "wifi";
        case PersistentLog::Category::Camera:
            return "camera";
        case PersistentLog::Category::Discovery:
            return "discovery";
        case PersistentLog::Category::Onvif:
            return "onvif";
        case PersistentLog::Category::Snapshot:
            return "snapshot";
        case PersistentLog::Category::Rtsp:
            return "rtsp";
        case PersistentLog::Category::Health:
            return "health";
        case PersistentLog::Category::Action:
            return "action";
        case PersistentLog::Category::Notification:
            return "notification";
        case PersistentLog::Category::Settings:
            return "settings";
    }

    return "unknown";
}

const char* outcomeName(uint8_t outcome)
{
    switch (static_cast<PersistentLog::Outcome>(outcome)) {
        case PersistentLog::Outcome::Info:
            return "info";
        case PersistentLog::Outcome::Started:
            return "started";
        case PersistentLog::Outcome::Success:
            return "success";
        case PersistentLog::Outcome::Failure:
            return "failure";
        case PersistentLog::Outcome::Skipped:
            return "skipped";
    }

    return "unknown";
}

const char* eventName(uint16_t event)
{
    switch (static_cast<PersistentLog::Event>(event)) {
        case PersistentLog::Event::SystemBoot:
            return "system_boot";
        case PersistentLog::Event::SystemRestart:
            return "system_restart";
        case PersistentLog::Event::WifiConnect:
            return "wifi_connect";
        case PersistentLog::Event::WifiDisconnect:
            return "wifi_disconnect";
        case PersistentLog::Event::WifiReconnect:
            return "wifi_reconnect";
        case PersistentLog::Event::CameraState:
            return "camera_state";
        case PersistentLog::Event::Incident:
            return "incident";
        case PersistentLog::Event::HealthCheck:
            return "health_check";
        case PersistentLog::Event::DiscoveryProbe:
            return "discovery_probe";
        case PersistentLog::Event::DiscoveryResult:
            return "discovery_result";
        case PersistentLog::Event::SubnetScan:
            return "subnet_scan";
        case PersistentLog::Event::TcpScan:
            return "tcp_scan";
        case PersistentLog::Event::OnvifProbe:
            return "onvif_probe";
        case PersistentLog::Event::OnvifSoap:
            return "onvif_soap";
        case PersistentLog::Event::SnapshotHttp:
            return "snapshot_http";
        case PersistentLog::Event::SnapshotTransfer:
            return "snapshot_transfer";
        case PersistentLog::Event::RtspProbe:
            return "rtsp_probe";
        case PersistentLog::Event::EmailHttp:
            return "email_http";
        case PersistentLog::Event::SettingsSaved:
            return "settings_saved";
        case PersistentLog::Event::SettingsReset:
            return "settings_reset";
        case PersistentLog::Event::AdminAction:
            return "admin_action";
        case PersistentLog::Event::CameraReboot:
            return "camera_reboot";
        case PersistentLog::Event::PtzCommand:
            return "ptz_command";
        case PersistentLog::Event::SntpSync:
            return "sntp_sync";
        case PersistentLog::Event::RuntimeContext:
            return "runtime_context";
        case PersistentLog::Event::SettingsContext:
            return "settings_context";
        case PersistentLog::Event::CameraTargetLoad:
            return "camera_target_load";
        case PersistentLog::Event::CameraTargetSave:
            return "camera_target_save";
        case PersistentLog::Event::DiscoveryResponse:
            return "discovery_response";
        case PersistentLog::Event::LogsCleared:
            return "logs_cleared";
    }

    return "unknown";
}

String ipAddress(uint32_t value)
{
    if (value == 0) {
        return String();
    }

    return String((value >> 24) & 0xFFU) + "." + String((value >> 16) & 0xFFU) + "." + String((value >> 8) & 0xFFU) + "." + String(value & 0xFFU);
}

}
