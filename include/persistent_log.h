#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace PersistentLog {

constexpr size_t CAPACITY = 7500;
constexpr size_t DETAIL_CAPACITY = 36;

enum class Level : uint8_t {
    Info,
    Warning,
    Error,
    Automatic = 0xFF
};

enum class Category : uint8_t {
    System,
    Wifi,
    Camera,
    Discovery,
    Onvif,
    Snapshot,
    Rtsp,
    Health,
    Action,
    Notification,
    Settings
};

enum class Outcome : uint8_t {
    Info,
    Started,
    Success,
    Failure,
    Skipped
};

enum class Event : uint16_t {
    SystemBoot,
    SystemRestart,
    WifiConnect,
    WifiDisconnect,
    WifiReconnect,
    CameraState,
    Incident,
    HealthCheck,
    DiscoveryProbe,
    DiscoveryResult,
    SubnetScan,
    TcpScan,
    OnvifProbe,
    OnvifSoap,
    SnapshotHttp,
    SnapshotTransfer,
    RtspProbe,
    EmailHttp,
    SettingsSaved,
    SettingsReset,
    AdminAction,
    CameraReboot,
    PtzCommand,
    SntpSync,
    RuntimeContext,
    SettingsContext,
    CameraTargetLoad,
    CameraTargetSave,
    DiscoveryResponse,
    LogsCleared
};

enum Flag : uint8_t {
    FlagNone = 0,
    FlagOutbound = 1U << 0,
    FlagConnectionReused = 1U << 1,
    FlagAuthenticationRetry = 1U << 2,
    FlagManual = 1U << 3,
    FlagScheduled = 1U << 4
};

struct Entry {
    uint16_t schemaVersion = 0;
    uint32_t sequence = 0;
    uint32_t epochSeconds = 0;
    uint32_t uptimeMs = 0;
    uint32_t targetIp = 0;
    uint32_t durationMs = 0;
    uint32_t value = 0;
    uint16_t bootId = 0;
    uint16_t contextId = 0;
    uint32_t cameraId = 0;
    uint16_t targetPort = 0;
    int16_t statusCode = 0;
    Event event = Event::SystemBoot;
    Category category = Category::System;
    Level level = Level::Info;
    Outcome outcome = Outcome::Info;
    uint8_t flags = FlagNone;
    char detail[DETAIL_CAPACITY] = {0};
};

bool begin();
bool ready();

void record(Event event, Outcome outcome, const char* detail = nullptr, const IPAddress& target = IPAddress(), uint16_t targetPort = 0, int statusCode = 0, uint32_t durationMs = 0, uint32_t value = 0, uint8_t flags = FlagNone, Level level = Level::Automatic, bool critical = false, uint16_t contextId = 0, uint32_t cameraId = 0);

void maintain();
bool flush();
bool clear();
bool readNewest(size_t offset, Entry& entry);

size_t count();
size_t capacity();
size_t pendingCount();
size_t storageTotalBytes();
size_t storageUsedBytes();
size_t storageFileBytes();
uint32_t droppedCount();
uint32_t writeErrorCount();

const char* levelName(Level level);
const char* categoryName(Category category);
const char* outcomeName(Outcome outcome);
const char* eventName(Event event);

}
