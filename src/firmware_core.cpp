#include "firmware_internal.h"
#include "camera_policy.h"

namespace CameraWatchdog {

const char FIRMWARE_VERSION[] = "0.24.0";

constexpr char RESEND_API_URL[] = "https://api.resend.com/emails";
constexpr char SETTINGS_NVS_NAMESPACE[] = "camcfg";
constexpr unsigned long ESP_SNTP_REFERENCE_MAX_AGE_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t GLOBAL_SETTINGS_MAGIC = 0x43573036UL;
constexpr uint32_t CAMERA_RECORD_MAGIC_V7 = 0x43414D37UL;
constexpr uint16_t CAMERA_RECORD_SCHEMA_VERSION = 7;
constexpr uint32_t CAMERA_ALIAS_MAGIC = 0x43414C37UL;
constexpr uint32_t CAMERA_REBOOT_LEDGER_MAGIC = 0x43524C37UL;
constexpr char CAMERAS_NVS_NAMESPACE[] = "cameras";
uint32_t nextCameraId = 1;
bool cameraRegistryFullyLoaded = true;

struct GlobalSettingsBlob {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    GlobalSettings values;
    uint32_t checksum;
};

struct CameraRecordHeaderV7 {
    uint32_t magic;
    uint32_t id;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    uint16_t urlBytes;
    uint16_t uuidBytes;
    uint16_t serialBytes;
    uint16_t manufacturerBytes;
    uint16_t modelBytes;
    uint16_t interfaceMacsBytes;
    uint8_t alternateCount;
    uint8_t reserved;
    uint16_t alternateBytes[CAMERA_ALTERNATE_ENDPOINT_CAPACITY];
    RuntimeSettings values;
    uint32_t checksum;
};

struct CameraAliasRecord {
    uint32_t magic;
    uint32_t secondaryId;
    uint32_t primaryId;
    uint32_t checksum;
};

struct CameraRebootLedger {
    uint32_t magic;
    uint32_t id;
    uint8_t count;
    uint8_t reserved[3];
    uint32_t checksum;
};

void resolveActiveIncident(CameraContext& ctx);

GlobalSettings globalSettings = makeDefaultGlobalSettings();
std::unique_ptr<CameraContext> cameras;
size_t cameraCount = 0;
uint32_t registryRevision = 1;
String lastRegistryResult = "Registry not loaded yet";
bool registryBusy = false;
bool discoveryRequested = false;
bool subnetScanRequested = false;
uint32_t pendingAdminCameraId = 0;
uint32_t activeAdminCameraId = 0;
uint32_t batchNextCameraId = 0;
uint32_t batchLastCameraId = 0;
uint32_t batchCompleted = 0;
uint32_t batchAccepted = 0;
uint32_t batchFailed = 0;
uint16_t cameraIoDepth = 0;

WebServer httpServer(HTTP_SERVER_PORT);
WiFiUDP discoveryUdp;
SemaphoreHandle_t cameraIoMutex = nullptr;

bool ensureCameraIoMutex()
{
    if (cameraIoMutex != nullptr) {
        return true;
    }

    cameraIoMutex = xSemaphoreCreateRecursiveMutex();
    return cameraIoMutex != nullptr;
}

bool settingsLoadedFromNvs = false;
uint32_t settingsRevision = 1;
uint32_t settingsWriteCount = 0;
String settingsWriteToken;
String lastSettingsResult = "Using factory values";
AdminAction pendingAdminAction = AdminAction::None;
AdminAction activeAdminAction = AdminAction::None;
uint16_t pendingAdminActionContextId = 0;
String lastAdminActionResult = "No admin action performed";
String pendingPtzPresetToken;
unsigned long espRestartRequestedAtMs = 0;
unsigned long factoryResetRequestedAtMs = 0;
bool factoryResetFailed = false;

char discoveryBuffer[WS_DISCOVERY_BUFFER_SIZE + 1];
size_t discoveredCount = 0;

unsigned long lastRediscoveryAtMs = 0;
unsigned long lastSubnetScanAtMs = 0;
unsigned long lastCameraAlertLedToggleAtMs = 0;

uint32_t discoveryRuns = 0;
uint32_t subnetScanRuns = 0;
uint16_t nextLogContextId = 1;
uint32_t heapBeforeBluetoothReleaseBytes = 0;
uint32_t heapAfterBluetoothReleaseBytes = 0;
uint32_t bluetoothMemoryReleasedBytes = 0;

bool cameraAlertLedOn = false;
bool cameraAlertLedBlinking = false;
bool bluetoothMemoryReleaseAttempted = false;
bool bluetoothStartedBeforeRelease = false;
bool connectedServicesInitialized = false;
bool mdnsAvailable = false;
bool wifiWasConnected = false;
bool healthCheckInProgress = false;
portMUX_TYPE sntpReferenceMux = portMUX_INITIALIZER_UNLOCKED;
bool espSntpSynchronized = false;
unsigned long espSntpSynchronizedAtMs = 0;
uint32_t espSntpSynchronizationGeneration = 0;
esp_err_t bluetoothMemoryReleaseResult = ESP_ERR_NOT_SUPPORTED;
esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease = ESP_BT_CONTROLLER_STATUS_NUM;

String lastDiscoveryResult = "Not run yet";
String lastScanResult = "Not run yet";

uint32_t settingsChecksumBytes(const void* values, size_t valueSize)
{
    return CameraPolicy::checksumBytes(values, valueSize);
}

template <typename T>
uint32_t settingsChecksum(const T& values)
{
    return settingsChecksumBytes(&values, sizeof(T));
}

size_t boundedTextLength(const char* value, size_t capacity)
{
    for (size_t index = 0; index < capacity; index++) {
        if (value[index] == '\0') {
            return index;
        }
    }

    return capacity;
}

bool settingTextValid(const char* value, size_t capacity, size_t minimumLength, size_t maximumLength)
{
    const size_t length = boundedTextLength(value, capacity);

    if (length >= capacity || length < minimumLength || length > maximumLength) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        const uint8_t byte = static_cast<uint8_t>(value[index]);

        if (byte < 0x20 || byte == 0x7F) {
            return false;
        }
    }

    return true;
}

bool localHostnameValid(const char* value, size_t capacity)
{
    if (!settingTextValid(value, capacity, LOCAL_HOSTNAME_MIN_LENGTH, LOCAL_HOSTNAME_MAX_LENGTH)) {
        return false;
    }

    const size_t length = boundedTextLength(value, capacity);

    if (value[0] == '-' || value[length - 1] == '-') {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        const char character = value[index];
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';

        if (!letter && !digit && character != '-') {
            return false;
        }
    }

    return true;
}

template <typename T>
bool settingInRange(T value, T minimum, T maximum)
{
    return value >= minimum && value <= maximum;
}

bool validateRuntimeSettings(const RuntimeSettings& values, String& error)
{
    if (!settingTextValid(values.cameraUsername, sizeof(values.cameraUsername), 0, CAMERA_USERNAME_MAX_LENGTH) || strchr(values.cameraUsername, ':') != nullptr) {
        error = "Invalid camera username; limit of 64 bytes, no colons or control characters";
        return false;
    }

    if (!settingTextValid(values.cameraPassword, sizeof(values.cameraPassword), 0, CAMERA_PASSWORD_MAX_LENGTH)) {
        error = "Invalid camera password; limit of 128 bytes, no control characters";
        return false;
    }

    if (values.cameraUsername[0] == '\0' && values.cameraPassword[0] != '\0') {
        error = "Enter the camera username or remove the credentials to access without authentication";
        return false;
    }

    if (!settingTextValid(values.cameraName, sizeof(values.cameraName), CAMERA_NAME_MIN_LENGTH, CAMERA_NAME_MAX_LENGTH)) {
        error = "Invalid camera name";
        return false;
    }

    if (!settingTextValid(values.snapshotProfileMatch, sizeof(values.snapshotProfileMatch), 0, SNAPSHOT_PROFILE_MATCH_MAX_LENGTH)) {
        error = "Invalid ONVIF profile filter";
        return false;
    }

#define CHECK_SETTING_RANGE(field, minimum, maximum, message) \
    if (!settingInRange(values.field, minimum, maximum)) { \
        error = message; \
        return false; \
    }

    CHECK_SETTING_RANGE(cameraRtspPort, CAMERA_RTSP_PORT_MIN, CAMERA_RTSP_PORT_MAX, "RTSP port out of range");
    CHECK_SETTING_RANGE(healthCheckIntervalMs, HEALTH_CHECK_INTERVAL_MIN_MS, HEALTH_CHECK_INTERVAL_MAX_MS, "Check interval out of range");
    CHECK_SETTING_RANGE(startupGraceMs, STARTUP_GRACE_MIN_MS, STARTUP_GRACE_MAX_MS, "Startup grace out of range");
    CHECK_SETTING_RANGE(failuresBeforeRediscovery, FAILURES_BEFORE_REDISCOVERY_MIN, FAILURES_BEFORE_REDISCOVERY_MAX, "Failures before rediscovery out of range");
    CHECK_SETTING_RANGE(failuresBeforeOffline, FAILURES_BEFORE_OFFLINE_MIN, FAILURES_BEFORE_OFFLINE_MAX, "Failures before offline out of range");
    CHECK_SETTING_RANGE(successesBeforeRecovery, SUCCESSES_BEFORE_RECOVERY_MIN, SUCCESSES_BEFORE_RECOVERY_MAX, "Successes before recovery out of range");
    CHECK_SETTING_RANGE(videoFailuresBeforeAlert, VIDEO_FAILURES_BEFORE_ALERT_MIN, VIDEO_FAILURES_BEFORE_ALERT_MAX, "Image failures before alert out of range");
    CHECK_SETTING_RANGE(videoFailuresBeforeReboot, VIDEO_FAILURES_BEFORE_REBOOT_MIN, VIDEO_FAILURES_BEFORE_REBOOT_MAX, "Image failures before reboot out of range");
    CHECK_SETTING_RANGE(onvifRebootGraceMs, ONVIF_REBOOT_GRACE_MIN_MS, ONVIF_REBOOT_GRACE_MAX_MS, "Post-reboot grace out of range");
    CHECK_SETTING_RANGE(onvifRebootCooldownMs, ONVIF_REBOOT_COOLDOWN_MIN_MS, ONVIF_REBOOT_COOLDOWN_MAX_MS, "Reboot cooldown out of range");
    CHECK_SETTING_RANGE(onvifRebootWindowMs, ONVIF_REBOOT_WINDOW_MIN_MS, ONVIF_REBOOT_WINDOW_MAX_MS, "Reboot window out of range");
    CHECK_SETTING_RANGE(maxOnvifRebootAttemptsPerWindow, MAX_ONVIF_REBOOT_ATTEMPTS_MIN, MAX_ONVIF_REBOOT_ATTEMPTS_MAX, "Reboot limit out of range");
    CHECK_SETTING_RANGE(tcpConnectTimeoutMs, TCP_CONNECT_TIMEOUT_MIN_MS, TCP_CONNECT_TIMEOUT_MAX_MS, "TCP timeout out of range");
    CHECK_SETTING_RANGE(serviceResponseTimeoutMs, SERVICE_RESPONSE_TIMEOUT_MIN_MS, SERVICE_RESPONSE_TIMEOUT_MAX_MS, "Protocol timeout out of range");
    CHECK_SETTING_RANGE(snapshotHttpTimeoutMs, SNAPSHOT_HTTP_TIMEOUT_MIN_MS, SNAPSHOT_HTTP_TIMEOUT_MAX_MS, "JPEG timeout out of range");
    CHECK_SETTING_RANGE(snapshotResolveRetryIntervalMs, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS, "Snapshot resolve interval out of range");
    CHECK_SETTING_RANGE(snapshotFailuresBeforeReresolve, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX, "Failures before re-resolving the snapshot out of range");
    CHECK_SETTING_RANGE(feedMinIntervalMs, FEED_MIN_INTERVAL_MIN_MS, FEED_MIN_INTERVAL_MAX_MS, "Minimum preview interval out of range");
    CHECK_SETTING_RANGE(feedTargetCycleMs, FEED_TARGET_CYCLE_MIN_MS, FEED_TARGET_CYCLE_MAX_MS, "Preview cycle out of range");
#undef CHECK_SETTING_RANGE

    if (values.enableOnvifAutoReboot > 1 || values.enableEmailAlerts > 1 || values.emailOnIpChange > 1) {
        error = "Invalid boolean option";
        return false;
    }

    if (values.failuresBeforeOffline < values.failuresBeforeRediscovery) {
        error = "Offline cannot happen before the rediscovery attempt";
        return false;
    }

    if (values.videoFailuresBeforeReboot < values.videoFailuresBeforeAlert) {
        error = "The reboot cannot happen before the image alert";
        return false;
    }

    if (values.onvifRebootWindowMs < values.onvifRebootCooldownMs) {
        error = "The reboot window must be greater than or equal to the cooldown";
        return false;
    }

    if (values.feedTargetCycleMs < values.feedMinIntervalMs) {
        error = "The preview cycle must be greater than or equal to the minimum interval";
        return false;
    }

    return true;
}

bool validateGlobalSettings(const GlobalSettings& values, String& error)
{
    if (!localHostnameValid(values.localHostname, sizeof(values.localHostname))) {
        error = "Invalid local address; use letters, numbers, and hyphens";
        return false;
    }

#define CHECK_SETTING_RANGE(field, minimum, maximum, message) \
    if (!settingInRange(values.field, minimum, maximum)) { \
        error = message; \
        return false; \
    }

    CHECK_SETTING_RANGE(wifiConnectTimeoutMs, WIFI_CONNECT_TIMEOUT_MIN_MS, WIFI_CONNECT_TIMEOUT_MAX_MS, "Wi-Fi timeout out of range");
    CHECK_SETTING_RANGE(discoveryTimeoutMs, DISCOVERY_TIMEOUT_MIN_MS, DISCOVERY_TIMEOUT_MAX_MS, "Discovery timeout out of range");
    CHECK_SETTING_RANGE(rediscoveryIntervalMs, REDISCOVERY_INTERVAL_MIN_MS, REDISCOVERY_INTERVAL_MAX_MS, "Rediscovery interval out of range");
    CHECK_SETTING_RANGE(subnetScanIntervalMs, SUBNET_SCAN_INTERVAL_MIN_MS, SUBNET_SCAN_INTERVAL_MAX_MS, "Scan interval out of range");
    CHECK_SETTING_RANGE(emailRetryIntervalMs, EMAIL_RETRY_INTERVAL_MIN_MS, EMAIL_RETRY_INTERVAL_MAX_MS, "Email interval out of range");
    CHECK_SETTING_RANGE(scanConnectTimeoutMs, SCAN_CONNECT_TIMEOUT_MIN_MS, SCAN_CONNECT_TIMEOUT_MAX_MS, "Scan timeout out of range");
    CHECK_SETTING_RANGE(cameraAlertLedBlinkIntervalMs, CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS, CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS, "LED interval out of range");
    CHECK_SETTING_RANGE(statusRefreshIntervalMs, STATUS_REFRESH_INTERVAL_MIN_MS, STATUS_REFRESH_INTERVAL_MAX_MS, "Dashboard refresh out of range");
#undef CHECK_SETTING_RANGE
    if (values.enableAutomaticSubnetScan > 1 || values.enableCameraAlertLed > 1) {
        error = "Invalid global boolean option";
        return false;
    }
    return true;
}

CameraContext* findCamera(uint32_t id)
{
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        if (ctx->id == id)
            return ctx;
    }
    return nullptr;
}

String normalizeCameraIdentifier(String value)
{
    char normalized[129];
    return CameraPolicy::normalizeIdentifier(value.c_str(), normalized) ? String(normalized) : String();
}

bool cameraIdentityMatches(const CameraTarget& left, const CameraTarget& right)
{
    const CameraPolicy::Identity leftIdentity{left.endpointUuid.c_str(), left.serialNumber.c_str(), left.manufacturer.c_str(), left.model.c_str(), left.interfaceMacs.c_str()};
    const CameraPolicy::Identity rightIdentity{right.endpointUuid.c_str(), right.serialNumber.c_str(), right.manufacturer.c_str(), right.model.c_str(), right.interfaceMacs.c_str()};
    return CameraPolicy::sameIdentity(leftIdentity, rightIdentity);
}

bool cameraIdWasMerged(uint32_t id, uint32_t* primaryId)
{
    char key[12];
    snprintf(key, sizeof(key), "m%08lx", static_cast<unsigned long>(id));
    CameraAliasRecord alias{};
    Preferences storage;
    if (!storage.begin(CAMERAS_NVS_NAMESPACE, true))
        return false;
    const bool loaded = storage.getBytesLength(key) == sizeof(alias) && storage.getBytes(key, &alias, sizeof(alias)) == sizeof(alias);
    const uint32_t checksum = alias.checksum;
    alias.checksum = 0;
    if (!loaded || alias.magic != CAMERA_ALIAS_MAGIC || alias.secondaryId != id || alias.primaryId == id || alias.primaryId == 0 || checksum != settingsChecksum(alias)) {
        storage.end();
        return false;
    }
    char primaryKey[12];
    snprintf(primaryKey, sizeof(primaryKey), "c%08lx", static_cast<unsigned long>(alias.primaryId));
    const size_t size = storage.getBytesLength(primaryKey);
    if (size < sizeof(CameraRecordHeaderV7) + 6 || size > sizeof(CameraRecordHeaderV7) + 2200) {
        storage.end();
        return false;
    }
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]);
    if (!bytes || storage.getBytes(primaryKey, bytes.get(), size) != size) {
        storage.end();
        return false;
    }
    storage.end();
    CameraRecordHeaderV7* primary = reinterpret_cast<CameraRecordHeaderV7*>(bytes.get());
    const uint32_t primaryChecksum = primary->checksum;
    primary->checksum = 0;
    size_t expectedSize = sizeof(*primary) + primary->urlBytes + primary->uuidBytes + primary->serialBytes + primary->manufacturerBytes + primary->modelBytes + primary->interfaceMacsBytes;
    if (primary->alternateCount > CAMERA_ALTERNATE_ENDPOINT_CAPACITY)
        return false;
    for (uint8_t index = 0; index < primary->alternateCount; index++)
        expectedSize += primary->alternateBytes[index];
    const char* url = reinterpret_cast<const char*>(bytes.get() + sizeof(*primary));
    ParsedUrl parsed;
    String settingsError;
    if (primary->magic != CAMERA_RECORD_MAGIC_V7 || primary->id != alias.primaryId || primary->schemaVersion != CAMERA_RECORD_SCHEMA_VERSION || primary->payloadSize != sizeof(RuntimeSettings) || primary->urlBytes == 0 || primary->uuidBytes == 0 || primary->serialBytes == 0 || primary->manufacturerBytes == 0 || primary->modelBytes == 0 || primary->interfaceMacsBytes == 0 || expectedSize != size || url[primary->urlBytes - 1] != '\0' || (url[0] != '\0' && (!parseHttpUrl(url, parsed) || parsed.secure)) || !validateRuntimeSettings(primary->values, settingsError) || !CameraPolicy::committedAliasCanHide(id, alias.primaryId, checksum == settingsChecksum(alias), settingsChecksumBytes(bytes.get(), size) == primaryChecksum))
        return false;
    if (primaryId != nullptr)
        *primaryId = alias.primaryId;
    return true;
}

bool persistCameraMergeAlias(uint32_t secondaryId, uint32_t primaryId)
{
    CameraAliasRecord alias{CAMERA_ALIAS_MAGIC, secondaryId, primaryId, 0};
    alias.checksum = settingsChecksum(alias);
    char key[12];
    snprintf(key, sizeof(key), "m%08lx", static_cast<unsigned long>(secondaryId));
    Preferences storage;
    if (!storage.begin(CAMERAS_NVS_NAMESPACE, false))
        return false;
    const bool saved = storage.putBytes(key, &alias, sizeof(alias)) == sizeof(alias);
    storage.end();
    return saved;
}

bool saveGlobalSettingsToNvs(const GlobalSettings& values)
{
    GlobalSettingsBlob blob{};
    blob.magic = GLOBAL_SETTINGS_MAGIC;
    blob.schemaVersion = RUNTIME_SETTINGS_SCHEMA_VERSION;
    blob.payloadSize = sizeof(values);
    blob.values = values;
    blob.checksum = settingsChecksum(values);
    Preferences storage;
    if (!storage.begin(SETTINGS_NVS_NAMESPACE, false))
        return false;
    const bool saved = storage.putBytes("global", &blob, sizeof(blob)) == sizeof(blob);
    storage.end();
    return saved;
}

bool saveRuntimeSettingsToNvs(CameraContext& ctx, const RuntimeSettings& values)
{
    const String& url = ctx.camera.onvifUrl;
    const String& uuid = ctx.camera.endpointUuid;
    const String& serial = ctx.camera.serialNumber;
    const String& manufacturer = ctx.camera.manufacturer;
    const String& model = ctx.camera.model;
    const String& interfaceMacs = ctx.camera.interfaceMacs;
    if (url.length() > 256 || uuid.length() > 128 || serial.length() > 128 || manufacturer.length() > 120 || model.length() > 120 || interfaceMacs.length() > 128 || ctx.alternateOnvifCount > CAMERA_ALTERNATE_ENDPOINT_CAPACITY)
        return false;
    size_t size = sizeof(CameraRecordHeaderV7) + url.length() + uuid.length() + serial.length() + manufacturer.length() + model.length() + interfaceMacs.length() + 6;
    for (uint8_t index = 0; index < ctx.alternateOnvifCount; index++) {
        if (ctx.alternateOnvifUrls[index].length() > 256)
            return false;
        size += ctx.alternateOnvifUrls[index].length() + 1;
    }
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]());
    if (!bytes)
        return false;
    CameraRecordHeaderV7* header = reinterpret_cast<CameraRecordHeaderV7*>(bytes.get());
    header->magic = CAMERA_RECORD_MAGIC_V7;
    header->id = ctx.id;
    header->schemaVersion = CAMERA_RECORD_SCHEMA_VERSION;
    header->payloadSize = sizeof(values);
    header->urlBytes = url.length() + 1;
    header->uuidBytes = uuid.length() + 1;
    header->serialBytes = serial.length() + 1;
    header->manufacturerBytes = manufacturer.length() + 1;
    header->modelBytes = model.length() + 1;
    header->interfaceMacsBytes = interfaceMacs.length() + 1;
    header->alternateCount = ctx.alternateOnvifCount;
    header->reserved = ctx.camera.valid ? 1 : 0;
    header->values = values;
    size_t offset = sizeof(*header);
    const auto append = [&](const String& text, uint16_t length) {
        memcpy(bytes.get() + offset, text.c_str(), length);
        offset += length;
    };
    append(url, header->urlBytes);
    append(uuid, header->uuidBytes);
    append(serial, header->serialBytes);
    append(manufacturer, header->manufacturerBytes);
    append(model, header->modelBytes);
    append(interfaceMacs, header->interfaceMacsBytes);
    for (uint8_t index = 0; index < ctx.alternateOnvifCount; index++) {
        header->alternateBytes[index] = ctx.alternateOnvifUrls[index].length() + 1;
        append(ctx.alternateOnvifUrls[index], header->alternateBytes[index]);
    }
    header->checksum = settingsChecksumBytes(bytes.get(), size);
    char key[12];
    snprintf(key, sizeof(key), "c%08lx", static_cast<unsigned long>(ctx.id));
    Preferences storage;
    if (!storage.begin(CAMERAS_NVS_NAMESPACE, false))
        return false;
    bool saved = storage.putBytes(key, bytes.get(), size) == size;
    const uint32_t storedLast = storage.getUInt("last_id", 0);
    if (saved && ctx.id > storedLast)
        saved = storage.putUInt("last_id", ctx.id) == sizeof(ctx.id);
    storage.end();
    return saved;
}

bool persistCamera(CameraContext& ctx)
{
    const bool saved = saveRuntimeSettingsToNvs(ctx, ctx.settings);
    if (saved)
        ctx.settingsLoadedFromNvs = true;
    return saved;
}

bool suspendCameraForIdentityConflict(CameraContext& ctx, const char* reason)
{
    ctx.camera.valid = false;
    invalidateSnapshotTarget(ctx);
    invalidateOnvifServiceCatalog(ctx);
    resetCameraSnapshotHttpSession(ctx);
    ctx.activeIncident = IncidentKind::None;
    ctx.pendingIncidentEmail = false;
    ctx.pendingRecoveryEmail = false;
    ctx.incidentAlertSent = false;
    ctx.incidentStartedAtMs = 0;
    ctx.cameraState = CameraState::Discovering;
    ctx.lastStateChangeAtMs = millis();
    ctx.lastEvent = reason;
    ctx.onvifMetadata.lastResult = reason;
    const bool saved = persistCamera(ctx);
    if (!saved)
        ctx.onvifMetadata.lastResult += "; NVS unavailable";
    registryRevision++;
    return saved;
}

bool automaticMergeSettingsCompatible(const CameraContext& primary, const CameraContext& secondary)
{
    RuntimeSettings primarySettings = primary.settings;
    RuntimeSettings secondarySettings = secondary.settings;
    memset(primarySettings.cameraName, 0, sizeof(primarySettings.cameraName));
    memset(secondarySettings.cameraName, 0, sizeof(secondarySettings.cameraName));
    const String primaryDefault = String("Camera ") + primary.id + " - " + primary.camera.ip.toString();
    const String secondaryDefault = String("Camera ") + secondary.id + " - " + secondary.camera.ip.toString();
    return strcmp(primary.settings.cameraName, primaryDefault.c_str()) == 0 && strcmp(secondary.settings.cameraName, secondaryDefault.c_str()) == 0 && memcmp(&primarySettings, &secondarySettings, sizeof(RuntimeSettings)) == 0;
}

CameraContext* registerCameraTarget(const CameraTarget& target, String& error)
{
    if (!target.valid || target.onvifUrl.isEmpty()) {
        error = "Invalid ONVIF endpoint";
        return nullptr;
    }
    ParsedUrl parsedTarget;
    if (!parseHttpUrl(target.onvifUrl, parsedTarget) || parsedTarget.secure || parsedTarget.ip == WiFi.localIP() || !CameraPolicy::safeHttpPath(parsedTarget.path.c_str(), 160, false)) {
        error = "ONVIF destination not allowed";
        return nullptr;
    }
    CameraContext* identityMatch = nullptr;
    size_t identityMatches = 0;
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        if (cameraIdentityMatches(ctx->camera, target)) {
            identityMatch = ctx;
            identityMatches++;
        }
    }
    if (identityMatches > 1) {
        error = "Repeated ONVIF identifier; automatic association suspended";
        return nullptr;
    }
    CameraContext* endpointMatch = nullptr;
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        if (ctx->camera.onvifUrl == target.onvifUrl) {
            endpointMatch = ctx;
            break;
        }
    }
    if (identityMatch != nullptr && endpointMatch != nullptr && endpointMatch != identityMatch) {
        const CameraPolicy::Identity endpointIdentity{endpointMatch->camera.endpointUuid.c_str(), endpointMatch->camera.serialNumber.c_str(), endpointMatch->camera.manufacturer.c_str(), endpointMatch->camera.model.c_str(), endpointMatch->camera.interfaceMacs.c_str()};
        const CameraPolicy::Identity observedIdentity{target.endpointUuid.c_str(), target.serialNumber.c_str(), target.manufacturer.c_str(), target.model.c_str(), target.interfaceMacs.c_str()};
        const bool descriptiveConflict = (!endpointMatch->camera.manufacturer.isEmpty() && !target.manufacturer.isEmpty() && !endpointMatch->camera.manufacturer.equalsIgnoreCase(target.manufacturer)) || (!endpointMatch->camera.model.isEmpty() && !target.model.isEmpty() && !endpointMatch->camera.model.equalsIgnoreCase(target.model));
        if (!CameraPolicy::identifiersConflict(endpointIdentity, observedIdentity) && !descriptiveConflict && automaticMergeSettingsCompatible(*identityMatch, *endpointMatch)) {
            if (mergeCameraRecords(identityMatch->id, endpointMatch->id, true, error))
                return registerCameraTarget(target, error);
            return nullptr;
        }
        error = "Address associated with another registration; review identity and settings";
        return nullptr;
    }
    CameraContext* ctx = identityMatch;
    if (ctx == nullptr && endpointMatch != nullptr) {
        const bool existingIdentified = !normalizeCameraIdentifier(endpointMatch->camera.endpointUuid).isEmpty() || !normalizeCameraIdentifier(endpointMatch->camera.serialNumber).isEmpty();
        const bool incomingIdentified = !normalizeCameraIdentifier(target.endpointUuid).isEmpty() || !normalizeCameraIdentifier(target.serialNumber).isEmpty();
        if (existingIdentified && incomingIdentified) {
            const String knownUuid = normalizeCameraIdentifier(endpointMatch->camera.endpointUuid);
            const String seenUuid = normalizeCameraIdentifier(target.endpointUuid);
            const String knownSerial = normalizeCameraIdentifier(endpointMatch->camera.serialNumber);
            const String seenSerial = normalizeCameraIdentifier(target.serialNumber);
            const bool descriptiveConflict = (!endpointMatch->camera.manufacturer.isEmpty() && !target.manufacturer.isEmpty() && !endpointMatch->camera.manufacturer.equalsIgnoreCase(target.manufacturer)) || (!endpointMatch->camera.model.isEmpty() && !target.model.isEmpty() && !endpointMatch->camera.model.equalsIgnoreCase(target.model));
            const bool conflict = (!knownUuid.isEmpty() && !seenUuid.isEmpty() && knownUuid != seenUuid) || (!knownSerial.isEmpty() && !seenSerial.isEmpty() && knownSerial != seenSerial) || descriptiveConflict;
            if (!conflict) {
                error = "Partial identity at this address; association pending";
                return nullptr;
            }
            if (endpointMatch->camera.valid && !suspendCameraForIdentityConflict(*endpointMatch, "Different identity at this address; access suspended")) {
                error = "Failed to suspend the reused address";
                return nullptr;
            }
            endpointMatch = nullptr;
        }
        if (existingIdentified && !incomingIdentified) {
            error = "Address identity not confirmed";
            return nullptr;
        }
        ctx = endpointMatch;
    }
    if (ctx != nullptr) {
        if (ctx->camera.onvifUrl == target.onvifUrl) {
            const CameraTarget previous = ctx->camera;
            ctx->camera.valid = true;
            if (ctx->camera.endpointUuid.isEmpty())
                ctx->camera.endpointUuid = target.endpointUuid;
            if (ctx->camera.serialNumber.isEmpty())
                ctx->camera.serialNumber = target.serialNumber;
            if (ctx->camera.manufacturer.isEmpty())
                ctx->camera.manufacturer = target.manufacturer;
            if (ctx->camera.model.isEmpty())
                ctx->camera.model = target.model;
            if (ctx->camera.interfaceMacs.isEmpty())
                ctx->camera.interfaceMacs = target.interfaceMacs;
            if (previous.valid != ctx->camera.valid || previous.endpointUuid != ctx->camera.endpointUuid || previous.serialNumber != ctx->camera.serialNumber || previous.manufacturer != ctx->camera.manufacturer || previous.model != ctx->camera.model || previous.interfaceMacs != ctx->camera.interfaceMacs) {
                if (!persistCamera(*ctx)) {
                    ctx->camera = previous;
                    error = "Failed to persist identity";
                    return nullptr;
                }
                registryRevision++;
            }
            return ctx;
        }
        for (uint8_t index = 0; index < ctx->alternateOnvifCount; index++) {
            if (ctx->alternateOnvifUrls[index] == target.onvifUrl)
                return ctx;
        }
        if (ctx->alternateOnvifCount >= CAMERA_ALTERNATE_ENDPOINT_CAPACITY || target.onvifUrl.length() > 256) {
            error = "Confirmed address limit reached";
            return nullptr;
        }
        const uint8_t index = ctx->alternateOnvifCount++;
        ctx->alternateOnvifUrls[index] = target.onvifUrl;
        if (!persistCamera(*ctx)) {
            ctx->alternateOnvifUrls[index] = String();
            ctx->alternateOnvifCount--;
            error = "Failed to persist alternate address";
            return nullptr;
        }
        registryRevision++;
        return ctx;
    }

    if (!cameraRegistryFullyLoaded) {
        error = "NVS registry incomplete; new registrations suspended to avoid duplication";
        return nullptr;
    }
    const size_t needed = sizeof(CameraContext) + target.onvifUrl.length() + target.endpointUuid.length() + target.scopes.length() + 2048;
    if (nextCameraId == 0 || ESP.getFreeHeap() < needed + CAMERA_OPERATION_HEAP_RESERVE || ESP.getMaxAllocHeap() < sizeof(CameraContext) + 4096) {
        error = "Not enough memory to register another camera while keeping monitoring running";
        return nullptr;
    }
    std::unique_ptr<CameraContext> entry(new (std::nothrow) CameraContext());
    if (!entry) {
        error = "Failed to allocate the camera state";
        return nullptr;
    }
    entry->id = nextCameraId;
    entry->registeredAtMs = millis();
    entry->lastStateChangeAtMs = entry->registeredAtMs;
    entry->camera = target;
    entry->camera.rtspPort = entry->settings.cameraRtspPort;
    copySettingText(entry->settings.cameraName, String("Camera ") + entry->id + " - " + target.ip.toString());
    if (!persistCamera(*entry)) {
        error = "NVS out of space or write failure; camera not registered";
        return nullptr;
    }
    CameraContext* result = entry.get();
    std::unique_ptr<CameraContext>* link = &cameras;
    while (*link)
        link = &(*link)->next;
    *link = std::move(entry);
    nextCameraId++;
    cameraCount++;
    registryRevision++;
    lastRegistryResult = String("Camera registered: ") + result->settings.cameraName;
    return result;
}

bool persistRebootLimiter(CameraContext& ctx)
{
    CameraRebootLedger ledger{};
    ledger.magic = CAMERA_REBOOT_LEDGER_MAGIC;
    ledger.id = ctx.id;
    ledger.count = ctx.cameraRebootAttemptsInWindow;
    ledger.checksum = settingsChecksum(ledger);
    char key[12];
    snprintf(key, sizeof(key), "r%08lx", static_cast<unsigned long>(ctx.id));
    Preferences storage;
    if (!storage.begin(CAMERAS_NVS_NAMESPACE, false))
        return false;
    const bool saved = storage.putBytes(key, &ledger, sizeof(ledger)) == sizeof(ledger);
    storage.end();
    return saved;
}

void loadRebootLimiter(CameraContext& ctx)
{
    char key[12];
    snprintf(key, sizeof(key), "r%08lx", static_cast<unsigned long>(ctx.id));
    Preferences storage;
    if (!storage.begin(CAMERAS_NVS_NAMESPACE, true))
        return;
    CameraRebootLedger ledger{};
    const bool loaded = storage.getBytesLength(key) == sizeof(ledger) && storage.getBytes(key, &ledger, sizeof(ledger)) == sizeof(ledger);
    storage.end();
    const uint32_t checksum = ledger.checksum;
    ledger.checksum = 0;
    if (!loaded || ledger.magic != CAMERA_REBOOT_LEDGER_MAGIC || ledger.id != ctx.id || checksum != settingsChecksum(ledger))
        return;
    ctx.cameraRebootAttemptsInWindow = min(ledger.count, static_cast<uint8_t>(MAX_ONVIF_REBOOT_ATTEMPTS_MAX));
    for (uint8_t index = 0; index < ctx.cameraRebootAttemptsInWindow; index++)
        ctx.cameraRebootAcceptedAtMs[index] = 1;
    if (ctx.cameraRebootAttemptsInWindow != 0) {
        ctx.cameraRebootWindowStartedAtMs = 1;
        ctx.lastCameraRebootAttemptAtMs = 1;
    }
}

bool mergeCameraRecords(uint32_t primaryId, uint32_t secondaryId, bool confirmSettings, String& error)
{
    if (primaryId == secondaryId || !confirmSettings || registryBusy || healthCheckInProgress || cameraIoDepth > 1 || currentAdminAction() != AdminAction::None) {
        error = "Merge requires two distinct registrations, confirmation, and an idle system";
        return false;
    }
    uint32_t existingPrimary = 0;
    if (cameraIdWasMerged(secondaryId, &existingPrimary))
        return existingPrimary == primaryId;
    CameraContext* primary = findCamera(primaryId);
    CameraContext* secondary = findCamera(secondaryId);
    if (primary == nullptr || secondary == nullptr) {
        error = "Registration not found";
        return false;
    }
    const String primaryUuid = normalizeCameraIdentifier(primary->camera.endpointUuid);
    const String secondaryUuid = normalizeCameraIdentifier(secondary->camera.endpointUuid);
    const String primarySerial = normalizeCameraIdentifier(primary->camera.serialNumber);
    const String secondarySerial = normalizeCameraIdentifier(secondary->camera.serialNumber);
    if ((!primaryUuid.isEmpty() && !secondaryUuid.isEmpty() && primaryUuid != secondaryUuid) || (!primarySerial.isEmpty() && !secondarySerial.isEmpty() && primarySerial != secondarySerial)) {
        error = "Conflicting ONVIF identifiers";
        return false;
    }
    const CameraTarget previousTarget = primary->camera;
    String previousAlternates[CAMERA_ALTERNATE_ENDPOINT_CAPACITY];
    const uint8_t previousCount = primary->alternateOnvifCount;
    for (uint8_t index = 0; index < previousCount; index++)
        previousAlternates[index] = primary->alternateOnvifUrls[index];
    if (primary->camera.endpointUuid.isEmpty())
        primary->camera.endpointUuid = secondary->camera.endpointUuid;
    if (primary->camera.serialNumber.isEmpty())
        primary->camera.serialNumber = secondary->camera.serialNumber;
    if (primary->camera.manufacturer.isEmpty())
        primary->camera.manufacturer = secondary->camera.manufacturer;
    if (primary->camera.model.isEmpty())
        primary->camera.model = secondary->camera.model;
    if (primary->camera.interfaceMacs.isEmpty())
        primary->camera.interfaceMacs = secondary->camera.interfaceMacs;
    const bool useSecondaryEndpoint = (secondary->cameraState == CameraState::Online || secondary->cameraState == CameraState::Degraded) && (primary->cameraState == CameraState::Offline || primary->cameraState == CameraState::Discovering || !primary->camera.valid);
    if (useSecondaryEndpoint) {
        const CameraTarget identifiedPrimary = primary->camera;
        primary->camera = secondary->camera;
        if (primary->camera.endpointUuid.isEmpty())
            primary->camera.endpointUuid = identifiedPrimary.endpointUuid;
        if (primary->camera.serialNumber.isEmpty())
            primary->camera.serialNumber = identifiedPrimary.serialNumber;
        if (primary->camera.manufacturer.isEmpty())
            primary->camera.manufacturer = identifiedPrimary.manufacturer;
        if (primary->camera.model.isEmpty())
            primary->camera.model = identifiedPrimary.model;
        if (primary->camera.interfaceMacs.isEmpty())
            primary->camera.interfaceMacs = identifiedPrimary.interfaceMacs;
    }
    const auto addEndpoint = [&](const String& url) {
        if (url.isEmpty() || url == primary->camera.onvifUrl)
            return true;
        for (uint8_t index = 0; index < primary->alternateOnvifCount; index++)
            if (primary->alternateOnvifUrls[index] == url)
                return true;
        if (primary->alternateOnvifCount >= CAMERA_ALTERNATE_ENDPOINT_CAPACITY)
            return false;
        primary->alternateOnvifUrls[primary->alternateOnvifCount++] = url;
        return true;
    };
    bool endpointsFit = useSecondaryEndpoint ? addEndpoint(previousTarget.onvifUrl) : addEndpoint(secondary->camera.onvifUrl);
    for (uint8_t index = 0; endpointsFit && index < secondary->alternateOnvifCount; index++)
        endpointsFit = addEndpoint(secondary->alternateOnvifUrls[index]);
    if (!endpointsFit || !persistCamera(*primary)) {
        primary->camera = previousTarget;
        primary->alternateOnvifCount = previousCount;
        for (uint8_t index = 0; index < CAMERA_ALTERNATE_ENDPOINT_CAPACITY; index++)
            primary->alternateOnvifUrls[index] = index < previousCount ? previousAlternates[index] : String();
        error = endpointsFit ? "Failed to write the merge to NVS; original registrations preserved" : "Confirmed address limit reached";
        return false;
    }
    if (secondary->cameraRebootAttemptsInWindow != 0) {
        const uint8_t combined = CameraPolicy::combinedRebootReservations(primary->cameraRebootAttemptsInWindow, secondary->cameraRebootAttemptsInWindow, MAX_ONVIF_REBOOT_ATTEMPTS_MAX);
        primary->cameraRebootAttemptsInWindow = combined;
        primary->lastCameraRebootAttemptAtMs = millis();
        primary->cameraRebootWindowStartedAtMs = primary->lastCameraRebootAttemptAtMs;
        for (size_t index = 0; index < MAX_ONVIF_REBOOT_ATTEMPTS_MAX; index++)
            primary->cameraRebootAcceptedAtMs[index] = index < combined ? primary->cameraRebootWindowStartedAtMs : 0;
    }
    if (!persistRebootLimiter(*primary) || !persistCameraMergeAlias(secondaryId, primaryId)) {
        primary->camera = previousTarget;
        primary->alternateOnvifCount = previousCount;
        for (uint8_t index = 0; index < CAMERA_ALTERNATE_ENDPOINT_CAPACITY; index++)
            primary->alternateOnvifUrls[index] = index < previousCount ? previousAlternates[index] : String();
        error = "Failed to write the limit or ID map; original registrations preserved";
        return false;
    }
    if (useSecondaryEndpoint) {
        invalidateSnapshotTarget(*primary);
        invalidateOnvifServiceCatalog(*primary);
        resetCameraSnapshotHttpSession(*primary);
        resolveActiveIncident(*primary);
        primary->cameraState = secondary->cameraState;
        primary->lastHealthCheckAtMs = secondary->lastHealthCheckAtMs;
        primary->lastSnapshotProbe = secondary->lastSnapshotProbe;
    }
    std::unique_ptr<CameraContext>* link = &cameras;
    while (link->get() != secondary)
        link = &(*link)->next;
    std::unique_ptr<CameraContext> removed = std::move(*link);
    *link = std::move(removed->next);
    cameraCount--;
    registryRevision++;
    lastRegistryResult = String("Registrations merged: ") + secondaryId + " -> " + primaryId;
    return true;
}

bool forgetCameraRecord(uint32_t id, String& error)
{
    if (!cameraRegistryFullyLoaded || registryBusy || healthCheckInProgress || cameraIoDepth > 1 || currentAdminAction() != AdminAction::None) {
        error = "Registry incomplete or another operation in progress";
        return false;
    }
    CameraContext* target = findCamera(id);
    if (target == nullptr) {
        error = "Registration not found";
        return false;
    }

    nvs_handle_t aliases;
    if (nvs_open(CAMERAS_NVS_NAMESPACE, NVS_READONLY, &aliases) != ESP_OK) {
        error = "NVS unavailable";
        return false;
    }
    uint32_t lastId = 0;
    nvs_stats_t stats{};
    if (nvs_get_u32(aliases, "last_id", &lastId) != ESP_OK || nvs_get_stats(nullptr, &stats) != ESP_OK || lastId < id || lastId > stats.total_entries) {
        nvs_close(aliases);
        error = "Invalid camera index in NVS";
        return false;
    }
    for (uint32_t otherId = 1; otherId <= lastId; otherId++) {
        if (otherId == id)
            continue;
        char aliasKey[12];
        snprintf(aliasKey, sizeof(aliasKey), "m%08lx", static_cast<unsigned long>(otherId));
        size_t size = 0;
        const esp_err_t sizeResult = nvs_get_blob(aliases, aliasKey, nullptr, &size);
        if (sizeResult == ESP_ERR_NVS_NOT_FOUND)
            continue;
        CameraAliasRecord alias{};
        if (sizeResult != ESP_OK || size != sizeof(alias) || nvs_get_blob(aliases, aliasKey, &alias, &size) != ESP_OK) {
            nvs_close(aliases);
            error = "Invalid association index in NVS";
            return false;
        }
        const uint32_t checksum = alias.checksum;
        alias.checksum = 0;
        if (alias.magic != CAMERA_ALIAS_MAGIC || alias.secondaryId != otherId || alias.primaryId == 0 || alias.primaryId == otherId || checksum != settingsChecksum(alias)) {
            nvs_close(aliases);
            error = "Invalid association index in NVS";
            return false;
        }
        if (alias.primaryId == id) {
            nvs_close(aliases);
            error = "Registration has other associated IDs; isolated removal blocked";
            return false;
        }
    }
    nvs_close(aliases);

    char key[12];
    snprintf(key, sizeof(key), "c%08lx", static_cast<unsigned long>(id));
    Preferences storage;
    if (!storage.begin(CAMERAS_NVS_NAMESPACE, false)) {
        error = "NVS unavailable";
        return false;
    }
    const bool removed = storage.getBytesLength(key) != 0 && storage.remove(key);
    storage.end();
    if (!removed) {
        error = "Failed to remove the registration from NVS; camera preserved";
        return false;
    }

    resetCameraSnapshotHttpSession(*target);
    std::unique_ptr<CameraContext>* link = &cameras;
    while (link->get() != target)
        link = &(*link)->next;
    std::unique_ptr<CameraContext> forgotten = std::move(*link);
    *link = std::move(forgotten->next);
    cameraCount--;
    registryRevision++;
    lastRegistryResult = String("Registration removed from NVS: ") + id;
    return true;
}

bool reconcileIdentifiedCameras()
{
    if (registryBusy || currentAdminAction() != AdminAction::None)
        return false;
    for (CameraContext* primary = cameras.get(); primary != nullptr; primary = primary->next.get()) {
        if (!cameraIdentityMatches(primary->camera, primary->camera))
            continue;
        for (CameraContext* secondary = primary->next.get(); secondary != nullptr; secondary = secondary->next.get()) {
            if (!cameraIdentityMatches(primary->camera, secondary->camera))
                continue;
            if (!automaticMergeSettingsCompatible(*primary, *secondary))
                continue;
            String error;
            return mergeCameraRecords(primary->id, secondary->id, true, error);
        }
    }
    return false;
}

void loadRuntimeSettings()
{
    GlobalSettingsBlob globalBlob{};
    Preferences storage;
    bool globalValid = false;
    if (storage.begin(SETTINGS_NVS_NAMESPACE, true)) {
        const size_t size = storage.getBytesLength("global");
        if (size == sizeof(globalBlob)) {
            globalValid = storage.getBytes("global", &globalBlob, sizeof(globalBlob)) == sizeof(globalBlob) && globalBlob.magic == GLOBAL_SETTINGS_MAGIC && globalBlob.schemaVersion == RUNTIME_SETTINGS_SCHEMA_VERSION && globalBlob.payloadSize == sizeof(GlobalSettings) && globalBlob.checksum == settingsChecksum(globalBlob.values);
            String error;
            globalValid = globalValid && validateGlobalSettings(globalBlob.values, error);
        }
        storage.end();
    }
    if (globalValid) {
        globalSettings = globalBlob.values;
        settingsLoadedFromNvs = true;
    }

    uint32_t lastId = 0;
    uint32_t missingCameraIds = 0;
    if (storage.begin(CAMERAS_NVS_NAMESPACE, true)) {
        lastId = storage.getUInt("last_id", 0);
        nvs_stats_t stats{};
        if (nvs_get_stats(nullptr, &stats) != ESP_OK || lastId > stats.total_entries) {
            lastRegistryResult = "Invalid camera index in NVS";
            cameraRegistryFullyLoaded = false;
            lastId = 0;
        }
        std::unique_ptr<CameraContext>* link = &cameras;
        for (uint32_t id = 1; id <= lastId; id++) {
            if (cameraIdWasMerged(id))
                continue;
            char key[12];
            snprintf(key, sizeof(key), "c%08lx", static_cast<unsigned long>(id));
            const size_t size = storage.getBytesLength(key);
            const CameraPolicy::StoredRecordLength recordLength = CameraPolicy::classifyStoredRecordLength(size, sizeof(CameraRecordHeaderV7) + 2, sizeof(CameraRecordHeaderV7) + 2200);
            if (recordLength == CameraPolicy::StoredRecordLength::MissingReserved) {
                missingCameraIds++;
                continue;
            }
            if (recordLength == CameraPolicy::StoredRecordLength::Invalid) {
                lastRegistryResult = String("Invalid registration: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            if (ESP.getFreeHeap() < sizeof(CameraContext) + size + CAMERA_OPERATION_HEAP_RESERVE) {
                lastRegistryResult = "Not enough memory to load all registrations; NVS preserved";
                cameraRegistryFullyLoaded = false;
                break;
            }
            std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]);
            if (!bytes || storage.getBytes(key, bytes.get(), size) != size) {
                lastRegistryResult = String("Failed to load camera ") + id;
                cameraRegistryFullyLoaded = false;
                break;
            }
            CameraRecordHeaderV7* record = reinterpret_cast<CameraRecordHeaderV7*>(bytes.get());
            const uint32_t checksum = record->checksum;
            record->checksum = 0;
            const bool headerValid = record->magic == CAMERA_RECORD_MAGIC_V7 && record->id == id && record->schemaVersion == CAMERA_RECORD_SCHEMA_VERSION && record->payloadSize == sizeof(RuntimeSettings) && record->alternateCount <= CAMERA_ALTERNATE_ENDPOINT_CAPACITY;
            const uint16_t urlBytes = record->urlBytes;
            const uint16_t uuidBytes = record->uuidBytes;
            size_t expected = sizeof(CameraRecordHeaderV7) + urlBytes + uuidBytes + record->serialBytes + record->manufacturerBytes + record->modelBytes + record->interfaceMacsBytes;
            for (uint8_t index = 0; headerValid && index < record->alternateCount; index++)
                expected += record->alternateBytes[index];
            if (!headerValid || urlBytes == 0 || uuidBytes == 0 || record->serialBytes == 0 || record->manufacturerBytes == 0 || record->modelBytes == 0 || record->interfaceMacsBytes == 0 || expected != size || settingsChecksumBytes(bytes.get(), size) != checksum) {
                lastRegistryResult = String("Integrity check failed: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            const char* url = reinterpret_cast<const char*>(bytes.get() + sizeof(CameraRecordHeaderV7));
            const char* uuid = url + urlBytes;
            if (url[urlBytes - 1] != '\0' || uuid[uuidBytes - 1] != '\0' || memchr(url, '\0', urlBytes - 1) != nullptr || memchr(uuid, '\0', uuidBytes - 1) != nullptr) {
                lastRegistryResult = String("Invalid address: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            ParsedUrl parsed;
            String error;
            const bool endpointKnown = url[0] != '\0';
            const RuntimeSettings& values = record->values;
            if ((endpointKnown && (!parseHttpUrl(url, parsed) || parsed.secure)) || !validateRuntimeSettings(values, error)) {
                lastRegistryResult = String("Invalid configuration: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            std::unique_ptr<CameraContext> entry(new (std::nothrow) CameraContext());
            if (!entry) {
                lastRegistryResult = "Not enough memory to load camera";
                cameraRegistryFullyLoaded = false;
                break;
            }
            entry->id = id;
            entry->settings = values;
            entry->settingsLoadedFromNvs = true;
            entry->registeredAtMs = millis();
            entry->lastStateChangeAtMs = entry->registeredAtMs;
            entry->camera.valid = endpointKnown && (record->reserved & 1) != 0;
            entry->camera.ip = parsed.ip;
            entry->camera.onvifPort = parsed.port;
            entry->camera.rtspPort = entry->settings.cameraRtspPort;
            entry->camera.onvifPath = parsed.path;
            entry->camera.onvifUrl = url;
            entry->camera.endpointUuid = uuid;
            const char* cursor = uuid + uuidBytes;
            const auto readField = [&](uint16_t length, String& result) {
                if (length == 0 || cursor[length - 1] != '\0' || memchr(cursor, '\0', length - 1) != nullptr)
                    return false;
                result = cursor;
                cursor += length;
                return true;
            };
            if (!readField(record->serialBytes, entry->camera.serialNumber) || !readField(record->manufacturerBytes, entry->camera.manufacturer) || !readField(record->modelBytes, entry->camera.model) || !readField(record->interfaceMacsBytes, entry->camera.interfaceMacs)) {
                lastRegistryResult = String("Invalid identity: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            bool alternateValid = true;
            for (uint8_t index = 0; index < record->alternateCount; index++) {
                String& alternative = entry->alternateOnvifUrls[index];
                ParsedUrl parsedAlternative;
                alternateValid = readField(record->alternateBytes[index], alternative) && parseHttpUrl(alternative, parsedAlternative) && !parsedAlternative.secure;
                if (!alternateValid)
                    break;
                entry->alternateOnvifCount++;
            }
            if (!alternateValid) {
                lastRegistryResult = String("Invalid alternate addresses: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            loadRebootLimiter(*entry);
            *link = std::move(entry);
            link = &(*link)->next;
            cameraCount++;
        }
        storage.end();
    }
    nextCameraId = lastId + 1;

    if (!globalValid && cameraRegistryFullyLoaded) {
        if (saveGlobalSettingsToNvs(globalSettings)) {
            settingsLoadedFromNvs = true;
            lastRegistryResult = "Initial registry ready";
        } else {
            lastRegistryResult = "Failed to write global settings";
        }
    } else if (cameraRegistryFullyLoaded) {
        lastRegistryResult = String(cameraCount) + " cameras loaded from NVS";
        if (missingCameraIds != 0)
            lastRegistryResult += String("; ") + missingCameraIds + " missing IDs reserved";
    }
    lastSettingsResult = settingsLoadedFromNvs ? "Settings loaded from NVS" : "Using factory values";
}

String makeSettingsWriteToken()
{
    char buffer[41];
    snprintf(buffer, sizeof(buffer), "%08lx%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
    return String(buffer);
}

bool adminPasswordConfigured()
{
    return strlen(DASHBOARD_ADMIN_PASSWORD_VALUE) > 0;
}

bool secureTextEquals(const String& supplied, const char* expected)
{
    const size_t suppliedLength = supplied.length();
    const size_t expectedLength = strlen(expected);
    uint8_t difference = static_cast<uint8_t>(suppliedLength == expectedLength ? 0 : 1);

    for (size_t index = 0; index < expectedLength; index++) {
        const uint8_t suppliedByte = index < suppliedLength ? static_cast<uint8_t>(supplied.charAt(index)) : 0;
        difference |= suppliedByte ^ static_cast<uint8_t>(expected[index]);
    }

    return difference == 0;
}

const char* adminActionName(AdminAction action)
{
    switch (action) {
        case AdminAction::None:
            return "none";
        case AdminAction::HealthCheck:
            return "health_check";
        case AdminAction::Rediscover:
            return "rediscover";
        case AdminAction::GotoPtzPreset:
            return "goto_ptz_preset";
        case AdminAction::RebootAllCameras:
            return "reboot-all-cameras";
        case AdminAction::CameraReboot:
            return "camera_reboot";
        case AdminAction::ResetRebootLimiter:
            return "reset_reboot_limiter";
        case AdminAction::RestartEsp32:
            return "restart_esp32";
    }

    return "unknown";
}

AdminAction currentAdminAction()
{
    return activeAdminAction != AdminAction::None ? activeAdminAction : pendingAdminAction;
}

const char* stateName(CameraState state)
{
    switch (state) {
        case CameraState::Starting:
            return "starting";
        case CameraState::Discovering:
            return "discovering";
        case CameraState::Online:
            return "online";
        case CameraState::Degraded:
            return "degraded";
        case CameraState::Recovering:
            return "recovering";
        case CameraState::Offline:
            return "offline";
    }

    return "unknown";
}

const char* resetReasonName(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external_pin";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
            return "task_watchdog";
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_UNKNOWN:
            return "unknown";
    }

    return "unknown";
}

const char* incidentName(IncidentKind kind)
{
    switch (kind) {
        case IncidentKind::None:
            return "none";
        case IncidentKind::SnapshotUnavailable:
            return "snapshot_unavailable";
        case IncidentKind::CameraOffline:
            return "camera_offline";
    }

    return "unknown";
}

const char* healthEvidenceSourceName(HealthEvidenceSource source)
{
    switch (source) {
        case HealthEvidenceSource::None:
            return "none";
        case HealthEvidenceSource::SnapshotJpeg:
            return "snapshot_jpeg";
        case HealthEvidenceSource::PreviewJpeg:
            return "preview_jpeg";
        case HealthEvidenceSource::LiveStream:
            return "live_stream";
    }

    return "unknown";
}

void beginOrEscalateIncident(CameraContext& ctx, IncidentKind kind)
{
    if (kind == IncidentKind::None) {
        return;
    }

    if (ctx.activeIncident == IncidentKind::None) {
        ctx.activeIncident = kind;
        ctx.consecutiveSuccesses = 0;
        ctx.incidentStartedAtMs = millis();
        ctx.incidentAlertSent = false;
        ctx.pendingIncidentEmail = false;
        PersistentLog::record(PersistentLog::Event::Incident, PersistentLog::Outcome::Started, incidentName(kind), ctx.camera.ip, ctx.camera.onvifPort, 0, 0, static_cast<uint32_t>(kind), PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true, 0, ctx.id);
        return;
    }

    if (ctx.activeIncident == IncidentKind::SnapshotUnavailable && kind == IncidentKind::CameraOffline) {
        ctx.activeIncident = kind;
        ctx.consecutiveSuccesses = 0;
        PersistentLog::record(PersistentLog::Event::Incident, PersistentLog::Outcome::Started, incidentName(kind), ctx.camera.ip, ctx.camera.onvifPort, 0, millis() - ctx.incidentStartedAtMs, static_cast<uint32_t>(kind), PersistentLog::FlagScheduled, PersistentLog::Level::Error, true, 0, ctx.id);

        if (ctx.incidentAlertSent) {
            ctx.incidentAlertSent = false;
            ctx.pendingIncidentEmail = true;
        }

        return;
    }

    if (ctx.activeIncident == IncidentKind::CameraOffline && kind == IncidentKind::SnapshotUnavailable && !ctx.incidentAlertSent) {
        ctx.activeIncident = kind;
        ctx.consecutiveSuccesses = 0;
    }
}

void queueIncidentAlert(CameraContext& ctx)
{
    if (ctx.settings.enableEmailAlerts && ctx.activeIncident != IncidentKind::None && !ctx.incidentAlertSent) {
        ctx.pendingIncidentEmail = true;
    }
}

void resolveActiveIncident(CameraContext& ctx)
{
    if (ctx.activeIncident == IncidentKind::None) {
        return;
    }

    if (ctx.incidentAlertSent && ctx.settings.enableEmailAlerts) {
        ctx.recoveryIncident = ctx.activeIncident;
        ctx.recoveryIncidentDurationMs = millis() - ctx.incidentStartedAtMs;
        ctx.pendingRecoveryEmail = true;
    }

    ctx.pendingIncidentEmail = false;
    ctx.incidentAlertSent = false;
    ctx.activeIncident = IncidentKind::None;
    ctx.incidentStartedAtMs = 0;
}

void writeCameraAlertLed(bool on)
{
    cameraAlertLedOn = on;
    digitalWrite(CAMERA_ALERT_LED_PIN, on ? HIGH : LOW);
}

void configureCameraAlertLed()
{
    pinMode(CAMERA_ALERT_LED_PIN, OUTPUT);
    writeCameraAlertLed(false);
    lastCameraAlertLedToggleAtMs = millis();
}

void updateCameraAlertLed()
{
    const unsigned long now = millis();

    if (!globalSettings.enableCameraAlertLed) {
        cameraAlertLedBlinking = false;
        lastCameraAlertLedToggleAtMs = now;

        if (cameraAlertLedOn) {
            writeCameraAlertLed(false);
        }

        return;
    }

    bool shouldBlink = WiFi.status() != WL_CONNECTED;
    for (const CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        shouldBlink = shouldBlink || ctx->cameraState == CameraState::Offline || ctx->cameraState == CameraState::Recovering || ctx->activeIncident != IncidentKind::None;
    }

    if (!shouldBlink) {
        cameraAlertLedBlinking = false;
        lastCameraAlertLedToggleAtMs = now;

        if (cameraAlertLedOn) {
            writeCameraAlertLed(false);
        }

        return;
    }

    if (!cameraAlertLedBlinking) {
        cameraAlertLedBlinking = true;
        lastCameraAlertLedToggleAtMs = now;
        writeCameraAlertLed(true);
        return;
    }

    if (now - lastCameraAlertLedToggleAtMs >= globalSettings.cameraAlertLedBlinkIntervalMs) {
        lastCameraAlertLedToggleAtMs = now;
        writeCameraAlertLed(!cameraAlertLedOn);
    }
}

void releaseUnusedBluetoothMemory()
{
    heapBeforeBluetoothReleaseBytes = ESP.getFreeHeap();
    heapAfterBluetoothReleaseBytes = heapBeforeBluetoothReleaseBytes;

#if RELEASE_BLUETOOTH_MEMORY
    bluetoothMemoryReleaseAttempted = true;
    bluetoothControllerStatusBeforeRelease = esp_bt_controller_get_status();

    bluetoothStartedBeforeRelease = btStarted();
    bluetoothMemoryReleaseResult = esp_bt_mem_release(ESP_BT_MODE_BTDM);
    heapAfterBluetoothReleaseBytes = ESP.getFreeHeap();

    if (heapAfterBluetoothReleaseBytes > heapBeforeBluetoothReleaseBytes) {
        bluetoothMemoryReleasedBytes = heapAfterBluetoothReleaseBytes - heapBeforeBluetoothReleaseBytes;
    }

    if (bluetoothMemoryReleaseResult == ESP_OK) {
        Serial.printf("Bluetooth memory released: %u bytes.\n", bluetoothMemoryReleasedBytes);
    } else if (bluetoothMemoryReleaseResult == ESP_ERR_NOT_FOUND) {
        Serial.println("Bluetooth memory was already released or was not reserved.");
    } else {
        Serial.printf("Could not release Bluetooth memory: %d\n", static_cast<int>(bluetoothMemoryReleaseResult));
    }

    Serial.printf("Bluetooth heap: before=%u after=%u difference=%u bytes\n", heapBeforeBluetoothReleaseBytes, heapAfterBluetoothReleaseBytes, bluetoothMemoryReleasedBytes);
#else
    Serial.println("Bluetooth memory release disabled.");
#endif
}

void appendJsonEscaped(String& output, const String& input)
{
    for (size_t index = 0; index < input.length(); index++) {
        const char value = input.charAt(index);

        switch (value) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (static_cast<uint8_t>(value) >= 0x20) {
                    output += value;
                }
                break;
        }
    }
}

String jsonEscape(const String& input)
{
    String output;
    output.reserve(input.length() + 16);
    appendJsonEscaped(output, input);
    return output;
}

void appendUnsigned64(String& output, uint64_t value)
{
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    output += buffer;
}

String htmlEscape(const String& input)
{
    String output;
    output.reserve(input.length() + 16);

    for (size_t index = 0; index < input.length(); index++) {
        const char value = input.charAt(index);

        switch (value) {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '"':
                output += "&quot;";
                break;
            case '\'':
                output += "&#39;";
                break;
            default:
                output += value;
                break;
        }
    }

    return output;
}

String currentTimestamp()
{
    const time_t now = time(nullptr);

    if (now < 1700000000) {
        return String("uptime-") + String(millis() / 1000UL) + "s";
    }

    struct tm utcTime;
    gmtime_r(&now, &utcTime);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return String(buffer);
}

bool cameraCredentialsConfigured(CameraContext& ctx)
{
    return ctx.settings.cameraUsername[0] != '\0';
}

void noteSntpSynchronization(struct timeval*)
{
    portENTER_CRITICAL(&sntpReferenceMux);
    espSntpSynchronized = true;
    espSntpSynchronizedAtMs = millis();

    if (espSntpSynchronizationGeneration < UINT32_MAX) {
        espSntpSynchronizationGeneration++;
    } else {
        espSntpSynchronizationGeneration = 1;
    }

    portEXIT_CRITICAL(&sntpReferenceMux);
}

bool clockSynchronized()
{
    return time(nullptr) >= 1700000000;
}

SntpReferenceSnapshot snapshotSntpReference()
{
    SntpReferenceSnapshot snapshot;
    portENTER_CRITICAL(&sntpReferenceMux);
    snapshot.synchronized = espSntpSynchronized;
    snapshot.synchronizedAtMs = espSntpSynchronizedAtMs;
    snapshot.generation = espSntpSynchronizationGeneration;
    portEXIT_CRITICAL(&sntpReferenceMux);
    return snapshot;
}

bool sntpReferenceFresh(const SntpReferenceSnapshot& snapshot)
{
    return snapshot.synchronized && millis() - snapshot.synchronizedAtMs <= ESP_SNTP_REFERENCE_MAX_AGE_MS && clockSynchronized();
}

String makeWsseSecurityHeader(CameraContext& ctx)
{
    if (!cameraCredentialsConfigured(ctx) || !clockSynchronized()) {
        return String();
    }

    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));

    const String created = currentTimestamp();
    uint8_t digest[20];
    mbedtls_sha1_context context;
    mbedtls_sha1_init(&context);

    bool digestOk = mbedtls_sha1_starts_ret(&context) == 0;
    digestOk = digestOk && mbedtls_sha1_update_ret(&context, nonce, sizeof(nonce)) == 0;
    digestOk = digestOk && mbedtls_sha1_update_ret(&context, reinterpret_cast<const unsigned char*>(created.c_str()), created.length()) == 0;
    digestOk = digestOk && mbedtls_sha1_update_ret(&context, reinterpret_cast<const unsigned char*>(ctx.settings.cameraPassword), strlen(ctx.settings.cameraPassword)) == 0;
    digestOk = digestOk && mbedtls_sha1_finish_ret(&context, digest) == 0;
    mbedtls_sha1_free(&context);

    if (!digestOk) {
        return String();
    }

    unsigned char nonceBase64[32];
    unsigned char digestBase64[40];
    size_t nonceLength = 0;
    size_t digestLength = 0;

    if (mbedtls_base64_encode(nonceBase64, sizeof(nonceBase64) - 1, &nonceLength, nonce, sizeof(nonce)) != 0 || mbedtls_base64_encode(digestBase64, sizeof(digestBase64) - 1, &digestLength, digest, sizeof(digest)) != 0) {
        return String();
    }

    nonceBase64[nonceLength] = '\0';
    digestBase64[digestLength] = '\0';

    String header;
    header.reserve(760);
    header += "<s:Header><wsse:Security s:mustUnderstand=\"1\" ";
    header += "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/";
    header += "oasis-200401-wss-wssecurity-secext-1.0.xsd\" ";
    header += "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/";
    header += "oasis-200401-wss-wssecurity-utility-1.0.xsd\">";
    header += "<wsse:UsernameToken><wsse:Username>";
    header += htmlEscape(ctx.settings.cameraUsername);
    header += "</wsse:Username><wsse:Password Type=\"http://docs.oasis-open.org/";
    header += "wss/2004/01/oasis-200401-wss-username-token-profile-1.0#";
    header += "PasswordDigest\">";
    header += reinterpret_cast<const char*>(digestBase64);
    header += "</wsse:Password><wsse:Nonce EncodingType=\"http://docs.oasis-open.org/";
    header += "wss/2004/01/oasis-200401-wss-soap-message-security-1.0#";
    header += "Base64Binary\">";
    header += reinterpret_cast<const char*>(nonceBase64);
    header += "</wsse:Nonce><wsu:Created>";
    header += created;
    header += "</wsu:Created></wsse:UsernameToken></wsse:Security></s:Header>";
    return header;
}

bool cameraRebootGraceActive(CameraContext& ctx)
{
    return ctx.cameraRebootGraceStartedAtMs != 0 && millis() - ctx.cameraRebootGraceStartedAtMs < ctx.settings.onvifRebootGraceMs;
}

unsigned long cameraRebootGraceRemainingMs(CameraContext& ctx)
{
    if (!cameraRebootGraceActive(ctx)) {
        return 0;
    }

    return ctx.settings.onvifRebootGraceMs - (millis() - ctx.cameraRebootGraceStartedAtMs);
}

void refreshCameraRebootWindow(CameraContext& ctx)
{
    const unsigned long now = millis();
    const uint8_t previousCount = ctx.cameraRebootAttemptsInWindow;
    uint8_t activeCount = 0;

    for (uint8_t index = 0; index < ctx.cameraRebootAttemptsInWindow; index++) {
        const unsigned long acceptedAtMs = ctx.cameraRebootAcceptedAtMs[index];

        if (acceptedAtMs != 0 && now - acceptedAtMs < ctx.settings.onvifRebootWindowMs) {
            ctx.cameraRebootAcceptedAtMs[activeCount++] = acceptedAtMs;
        }
    }

    for (uint8_t index = activeCount; index < MAX_ONVIF_REBOOT_ATTEMPTS_MAX; index++) {
        ctx.cameraRebootAcceptedAtMs[index] = 0;
    }

    ctx.cameraRebootAttemptsInWindow = activeCount;
    ctx.cameraRebootWindowStartedAtMs = activeCount > 0 ? ctx.cameraRebootAcceptedAtMs[0] : 0;
    if (activeCount != previousCount)
        persistRebootLimiter(ctx);
}

String durationText(unsigned long durationMs)
{
    const unsigned long totalSeconds = durationMs / 1000UL;
    const unsigned long hours = totalSeconds / 3600UL;
    const unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    const unsigned long seconds = totalSeconds % 60UL;

    char buffer[48];
    snprintf(buffer, sizeof(buffer), "%luh %lum %lus", hours, minutes, seconds);

    return String(buffer);
}

void applyRuntimeSettings(CameraContext& ctx, const RuntimeSettings& previous)
{
    const bool credentialsChanged = strcmp(previous.cameraUsername, ctx.settings.cameraUsername) != 0 || strcmp(previous.cameraPassword, ctx.settings.cameraPassword) != 0;
    if (credentialsChanged || strcmp(previous.snapshotProfileMatch, ctx.settings.snapshotProfileMatch) != 0) {
        invalidateSnapshotTarget(ctx);
        if (credentialsChanged)
            invalidateOnvifServiceCatalog(ctx);
        ctx.lastSnapshotResolveAttemptAtMs = 0;
        ctx.lastSnapshotResolveStatusCode = 0;
        ctx.lastSnapshotResolveTransportFailure = false;
        ctx.lastSnapshotResult = "Configuration changed; waiting for a new JPEG test";
        resetCameraCheckSequences(ctx, ctx.lastSnapshotResult.c_str());
        ctx.lastHealthCheckAtMs = 0;
    }
    ctx.camera.rtspPort = ctx.settings.cameraRtspPort;
    if (!ctx.settings.enableEmailAlerts) {
        ctx.pendingIncidentEmail = false;
        ctx.pendingRecoveryEmail = false;
        ctx.pendingIpChangeEmail = false;
    }
    if (previous.healthCheckIntervalMs != ctx.settings.healthCheckIntervalMs)
        ctx.lastHealthCheckAtMs = 0;
    refreshCameraRebootWindow(ctx);
    registryRevision++;
}

void applyGlobalSettings(const GlobalSettings& previous)
{
    if (strcmp(previous.localHostname, globalSettings.localHostname) != 0 && connectedServicesInitialized && WiFi.status() == WL_CONNECTED) {
        MDNS.end();
        startMdns();
    }
    if (!globalSettings.enableCameraAlertLed) {
        cameraAlertLedBlinking = false;
        writeCameraAlertLed(false);
    }
}

void setState(CameraContext& ctx, CameraState newState, const String& event)
{
    const bool stateChanged = ctx.cameraState != newState;

    if (stateChanged) {
        ctx.cameraState = newState;
        ctx.lastStateChangeAtMs = millis();

        PersistentLog::Level level = PersistentLog::Level::Info;

        if (newState == CameraState::Degraded || newState == CameraState::Recovering) {
            level = PersistentLog::Level::Warning;
        } else if (newState == CameraState::Offline) {
            level = PersistentLog::Level::Error;
        }

        PersistentLog::record(PersistentLog::Event::CameraState, PersistentLog::Outcome::Info, stateName(newState), ctx.camera.ip, ctx.camera.onvifPort, 0, 0, static_cast<uint32_t>(newState), PersistentLog::FlagScheduled, level, true, 0, ctx.id);
    }

    ctx.lastEvent = currentTimestamp() + " - " + event;
    Serial.printf("STATE: %s | %s\n", stateName(ctx.cameraState), event.c_str());
}

bool resendConfigured()
{
    return String(RESEND_API_KEY).startsWith("re_") && String(RESEND_FROM).indexOf("example.com") < 0 && String(ALERT_EMAIL).indexOf("example.com") < 0;
}

class BoundedStringStream : public Stream {
public:
    BoundedStringStream(String& output, size_t limit) : output_(output), limit_(limit)
    {}

    size_t write(uint8_t value) override
    {
        return write(&value, 1);
    }

    size_t write(const uint8_t* buffer, size_t size) override
    {
        const size_t outputLength = output_.length();

        if (buffer == nullptr || outputLength > limit_ || size > limit_ - outputLength) {
            overflowed_ = true;
            setWriteError();
            return 0;
        }

        if (!output_.concat(reinterpret_cast<const char*>(buffer), static_cast<unsigned int>(size))) {
            allocationFailed_ = true;
            setWriteError();
            return 0;
        }

        return size;
    }

    int available() override
    {
        return 0;
    }

    int read() override
    {
        return -1;
    }

    int peek() override
    {
        return -1;
    }

    bool overflowed() const
    {
        return overflowed_;
    }

    bool allocationFailed() const
    {
        return allocationFailed_;
    }

private:
    String& output_;
    size_t limit_;
    bool overflowed_ = false;
    bool allocationFailed_ = false;
};

bool readHttpResponseBodyBounded(HTTPClient& request, String& output, size_t limit, bool& limitExceeded, bool* streamFailure)
{
    output = String();
    limitExceeded = false;

    if (streamFailure != nullptr) {
        *streamFailure = false;
    }

    const int declaredSize = request.getSize();

    if (declaredSize == 0) {
        return true;
    }

    if (declaredSize > 0 && static_cast<size_t>(declaredSize) > limit) {
        limitExceeded = true;
        return false;
    }

    const size_t reserveSize = declaredSize > 0 ? static_cast<size_t>(declaredSize) + 1 : min(limit + 1, static_cast<size_t>(1024));

    if (reserveSize > 0 && !output.reserve(reserveSize)) {
        return false;
    }

    BoundedStringStream sink(output, limit);
    const int bytesWritten = request.writeToStream(&sink);
    limitExceeded = sink.overflowed();
    const bool incompleteTransfer = bytesWritten < 0 || (declaredSize >= 0 && bytesWritten != declaredSize);

    if (streamFailure != nullptr && incompleteTransfer && !limitExceeded && !sink.allocationFailed()) {
        *streamFailure = true;
    }

    return bytesWritten >= 0 && !limitExceeded && (declaredSize < 0 || bytesWritten == declaredSize);
}

bool sendResendEmail(CameraContext& ctx, const String& subject, const String& html)
{
    ctx.lastEmailAttemptAtMs = millis();
    const unsigned long startedAtMs = millis();

    if (!resendConfigured()) {
        ctx.lastEmailResult = "Resend is not configured in include/secrets.h";
        Serial.println(ctx.lastEmailResult);
        ctx.emailErrors++;
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Skipped, "resend_not_configured", IPAddress(), 443, 0, millis() - startedAtMs, 0, PersistentLog::FlagOutbound, PersistentLog::Level::Automatic, false, 0, ctx.id);
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        ctx.lastEmailResult = "Wi-Fi disconnected; sending deferred";
        ctx.emailErrors++;
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Skipped, "wifi_disconnected", IPAddress(), 443, 0, millis() - startedAtMs, 0, PersistentLog::FlagOutbound, PersistentLog::Level::Automatic, false, 0, ctx.id);
        return false;
    }

    WiFiClientSecure secureClient;

    secureClient.setInsecure();

    HTTPClient request;
    request.setConnectTimeout(10000);
    request.setTimeout(15000);
    request.setReuse(false);

    if (!request.begin(secureClient, RESEND_API_URL)) {
        ctx.lastEmailResult = "Failed to initialize the HTTPS client";
        ctx.emailErrors++;
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Failure, "https_begin_failed", IPAddress(), 443, -1, millis() - startedAtMs, 0, PersistentLog::FlagOutbound, PersistentLog::Level::Automatic, false, 0, ctx.id);
        return false;
    }

    request.addHeader("Authorization", String("Bearer ") + RESEND_API_KEY);
    request.addHeader("Content-Type", "application/json");
    request.addHeader("User-Agent", String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION);

    String payload;
    payload.reserve(subject.length() + html.length() + html.length() / 4 + 512);
    payload += "{\"from\":\"";
    appendJsonEscaped(payload, RESEND_FROM);
    payload += "\",\"to\":[\"";
    appendJsonEscaped(payload, ALERT_EMAIL);
    payload += "\"],\"subject\":\"";
    appendJsonEscaped(payload, subject);
    payload += "\",\"html\":\"";
    appendJsonEscaped(payload, html);
    payload += "\"}";

    const int statusCode = request.POST(payload);
    String responseBody;
    bool responseLimitExceeded = false;
    const bool responseRead = statusCode > 0 && readHttpResponseBodyBounded(request, responseBody, MAX_RESEND_RESPONSE_SIZE, responseLimitExceeded);
    request.end();

    if (responseLimitExceeded) {
        responseBody = "API response exceeded the safe limit";
    } else if (statusCode > 0 && !responseRead) {
        responseBody = "API response unavailable";
    }

    if (statusCode >= 200 && statusCode < 300) {
        ctx.emailsSent++;
        ctx.lastEmailResult = currentTimestamp() + " - sent; HTTP " + String(statusCode) + "; " + responseBody;

        Serial.println(ctx.lastEmailResult);
        return true;
    }

    ctx.emailErrors++;
    ctx.lastEmailResult = currentTimestamp() + " - error; HTTP " + String(statusCode) + "; " + responseBody;

    Serial.println(ctx.lastEmailResult);
    PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Failure, "resend_email", IPAddress(), 443, statusCode, millis() - startedAtMs, static_cast<uint32_t>(payload.length()), PersistentLog::FlagOutbound, PersistentLog::Level::Automatic, false, 0, ctx.id);
    return false;
}

struct EmailTheme {
    const char* accent;
    const char* soft;
};

constexpr EmailTheme EMAIL_THEME_OK{"#0d9668", "#e6f7f0"};
constexpr EmailTheme EMAIL_THEME_ALERT{"#dc2626", "#fdecec"};
constexpr EmailTheme EMAIL_THEME_INFO{"#2563eb", "#eaf1fe"};

const char* stateLabel(CameraState state)
{
    switch (state) {
        case CameraState::Starting:
            return "starting";
        case CameraState::Discovering:
            return "looking for the camera";
        case CameraState::Online:
            return "online";
        case CameraState::Degraded:
            return "unstable";
        case CameraState::Recovering:
            return "recovering";
        case CameraState::Offline:
            return "offline";
    }

    return "unknown";
}

String describeProbe(const ServiceProbe& probe, const char* protocol)
{
    if (!probe.tcpConnected) {
        return "no TCP connection";
    }

    String text;

    if (probe.protocolResponded) {
        text = "responded with ";
        text += protocol;
        text += ' ';
        text += probe.statusCode;
    } else {
        text = "port open, no protocol response";
    }

    text += " · ";
    text += probe.latencyMs;
    text += " ms";
    return text;
}

void appendDetailRow(String& html, const char* label, const String& value, uint8_t index)
{
    html += "<tr style=\"background:";
    html += (index % 2) ? "#f8fafc" : "#ffffff";
    html += "\"><td style=\"padding:11px 18px;font-size:13px;line-height:1.45;" "color:#64748b;white-space:nowrap;vertical-align:top;";

    if (index > 0) {
        html += "border-top:1px solid #edf2f7;";
    }

    html += "\">";
    html += htmlEscape(label);
    html += "</td><td style=\"padding:11px 18px;font-size:13px;line-height:1.45;" "color:#0f1b2b;font-weight:600;word-break:break-word;vertical-align:top;";

    if (index > 0) {
        html += "border-top:1px solid #edf2f7;";
    }

    html += "\">";
    html += htmlEscape(value);
    html += "</td></tr>";
}

void appendDetailsTable(CameraContext& ctx, String& html)
{
    html += "<tr><td style=\"padding:24px 28px 0\">" "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\" style=\"border-collapse:separate;" "border-spacing:0;border:1px solid #e3e9f0;border-radius:10px;" "overflow:hidden\">";

    uint8_t index = 0;

    const auto row = [&](const char* label, const String& value) {
        appendDetailRow(html, label, value, index);
        index++;
    };

    String snapshotDescription = ctx.lastSnapshotProbe.detail;

    if (ctx.lastSnapshotProbe.jpegValid) {
        snapshotDescription = String("Valid JPEG · ") + String(ctx.lastSnapshotProbe.bytesRead) + " bytes · " + String(ctx.lastSnapshotProbe.latencyMs) + " ms";
    }

    row("Camera", ctx.settings.cameraName);
    row("State", stateLabel(ctx.cameraState));
    row("IP address", ctx.camera.valid ? ctx.camera.ip.toString() : "not found");
    row("ONVIF endpoint", ctx.camera.onvifUrl.isEmpty() ? String("unknown") : ctx.camera.onvifUrl);
    row("Image test", snapshotDescription);
    row("ONVIF test", describeProbe(ctx.lastOnvifProbe, "HTTP"));
    row("RTSP test", ctx.lastRtspProbeAttempted ? describeProbe(ctx.lastRtspProbe, "RTSP") : ctx.lastOnvifProbe.protocolResponded ? String("skipped; ONVIF responded") : String("not run; waiting for the camera and ONVIF"));
    row("Consecutive image failures", String(ctx.consecutiveFailures));
    row("Consecutive control failures", String(ctx.consecutiveControlFailures));
    row("ONVIF recovery", ctx.lastCameraRebootResult);
    row("Checks", String(ctx.totalHealthChecks) + " total · " + String(ctx.failedHealthChecks) + " failed");
    row("ESP32", WiFi.localIP().toString() + " · " + WiFi.SSID() + " · " + String(WiFi.RSSI()) + " dBm");
    row("Uptime", durationText(millis()));
    row("Time (UTC)", currentTimestamp());

    html += "</table></td></tr>";
}

void appendCallout(String& html, const EmailTheme& theme, const String& text)
{
    html += "<tr><td style=\"padding:22px 28px 0\">" "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\"><tr><td style=\"padding:13px 17px;" "background:";
    html += theme.soft;
    html += ";border-left:3px solid ";
    html += theme.accent;
    html += ";border-radius:0 8px 8px 0;font-size:14px;line-height:1.5;color:#334155\">";
    html += text;
    html += "</td></tr></table></td></tr>";
}

String buildEmailHtml(CameraContext& ctx, const EmailTheme& theme, const char* badge, const char* title, const char* intro, const String& callout)
{
    String html;
    html.reserve(10240);

    html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">" "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" "<meta name=\"x-apple-disable-message-reformatting\"></head>" "<body style=\"margin:0;padding:0;background:#eef2f7\">" "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\" style=\"background:#eef2f7;" "padding:26px 12px\"><tr><td align=\"center\">" "<table role=\"presentation\" width=\"600\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\" style=\"width:100%;max-width:600px;" "background:#ffffff;border:1px solid #dfe6ee;border-radius:14px;" "overflow:hidden;font-family:-apple-system,BlinkMacSystemFont," "'Segoe UI',Roboto,Helvetica,Arial,sans-serif\">";

    html += "<tr><td style=\"height:4px;line-height:4px;font-size:0;background:";
    html += theme.accent;
    html += "\">&nbsp;</td></tr>";

    html += "<tr><td style=\"padding:26px 28px 0\">" "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\"><tr>" "<td style=\"font-size:11px;font-weight:700;letter-spacing:.11em;" "text-transform:uppercase;color:#94a3b8\">Camera Watchdog</td>" "<td align=\"right\"><span style=\"display:inline-block;" "padding:5px 12px;border-radius:999px;font-size:11px;font-weight:700;" "letter-spacing:.05em;background:";
    html += theme.soft;
    html += ";color:";
    html += theme.accent;
    html += "\">";
    html += badge;
    html += "</span></td></tr></table></td></tr>";

    html += "<tr><td style=\"padding:16px 28px 0\">" "<h1 style=\"margin:0;font-size:23px;line-height:1.25;color:#0f1b2b;" "font-weight:700;letter-spacing:-.01em\">";
    html += title;
    html += "</h1><p style=\"margin:9px 0 0;font-size:15px;line-height:1.6;" "color:#51617a\">";
    html += intro;
    html += "</p></td></tr>";

    if (!callout.isEmpty()) {
        appendCallout(html, theme, callout);
    }

    appendDetailsTable(ctx, html);

    html += "<tr><td style=\"padding:24px 28px 0\">" "<a href=\"http://";
    html += WiFi.localIP().toString();
    html += "\" style=\"display:inline-block;background:#0f1b2b;color:#ffffff;" "text-decoration:none;padding:12px 22px;border-radius:9px;" "font-size:14px;font-weight:600\">Open the device dashboard</a>" "</td></tr>";

    html += "<tr><td style=\"padding:24px 28px 26px\">" "<div style=\"border-top:1px solid #edf2f7;padding-top:16px;" "font-size:11.5px;line-height:1.6;color:#94a3b8\">" "Automatic message from the ESP32 Camera Watchdog · firmware ";
    html += FIRMWARE_VERSION;
    html += " · dashboard at http://";
    html += WiFi.localIP().toString();
    html += "</div></td></tr>";

    html += "</table></td></tr></table></body></html>";
    return html;
}

bool sendIncidentAlert(CameraContext& ctx)
{
    if (ctx.activeIncident == IncidentKind::None) {
        ctx.pendingIncidentEmail = false;
        return true;
    }

    const bool snapshotOnly = ctx.activeIncident == IncidentKind::SnapshotUnavailable;
    const uint8_t snapshotFailureCount = ctx.lastSnapshotProbe.conclusive ? ctx.consecutiveFailures : ctx.consecutiveInconclusiveChecks;

    String subject = snapshotOnly ? String("[Snapshot unavailable] ") : String("[Camera offline] ");
    subject += ctx.settings.cameraName;

    String callout;
    callout.reserve(640);

    if (snapshotOnly) {
        callout = "The camera still responds to the control services, but it failed " "<strong>";
        callout += snapshotFailureCount;
        callout += "</strong> consecutive snapshot checks. Last test: ";
        callout += htmlEscape(ctx.lastSnapshotProbe.detail);
        callout += ".";
    } else {
        callout = "The ESP32 could not get an image or confirm an " "ONVIF/RTSP response. Consecutive failures: image <strong>";
        callout += ctx.consecutiveFailures;
        callout += "</strong>, control <strong>";
        callout += ctx.consecutiveControlFailures;
        callout += "</strong>.";
    }

    if (ctx.lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Automatic recovery: ";
        callout += htmlEscape(ctx.lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(ctx, EMAIL_THEME_ALERT, snapshotOnly ? "NO SNAPSHOT" : "OFFLINE", snapshotOnly ? "The camera stopped delivering JPEG snapshots" : "The camera stopped responding", snapshotOnly ? "The control service is still reachable, but the ESP32 did not " "receive a complete, valid JPEG in the latest checks." : "The outage was confirmed by independent tests of " "image, ONVIF and RTSP.", callout);

    const bool sent = sendResendEmail(ctx, subject, html);

    if (sent) {
        ctx.incidentAlertSent = true;
        ctx.pendingIncidentEmail = false;
    }

    return sent;
}

bool sendRecoveryAlert(CameraContext& ctx)
{
    if (ctx.recoveryIncident == IncidentKind::None) {
        ctx.pendingRecoveryEmail = false;
        return true;
    }

    const bool recoveredSnapshot = ctx.recoveryIncident == IncidentKind::SnapshotUnavailable;

    String subject = recoveredSnapshot ? String("[Snapshot recovered] ") : String("[Camera recovered] ");
    subject += ctx.settings.cameraName;

    String callout = "Approximate incident duration: <strong>";
    callout += durationText(ctx.recoveryIncidentDurationMs);
    callout += (recoveredSnapshot ? "</strong>. The current test received a complete, valid JPEG." : "</strong>. The current checks confirmed that the camera is responding again.");

    if (ctx.lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Last ONVIF recovery: ";
        callout += htmlEscape(ctx.lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(ctx, EMAIL_THEME_OK, "RECOVERED", recoveredSnapshot ? "JPEG snapshots are working again" : "The camera is responding again", "Two consecutive checks confirmed the recovery. " "Normal monitoring has resumed.", callout);

    const bool sent = sendResendEmail(ctx, subject, html);

    if (sent) {
        ctx.pendingRecoveryEmail = false;
        ctx.recoveryIncident = IncidentKind::None;
        ctx.recoveryIncidentDurationMs = 0;
    }

    return sent;
}

bool sendIpChangeAlert(CameraContext& ctx, const String& oldIp, const String& newIp)
{
    if (!ctx.settings.enableEmailAlerts || !ctx.settings.emailOnIpChange || oldIp.isEmpty() || oldIp == newIp) {
        return true;
    }

    String subject = "[Camera IP changed] ";
    subject += ctx.settings.cameraName;

    String callout = "<strong>";
    callout += htmlEscape(oldIp);
    callout += "</strong> &rarr; <strong>";
    callout += htmlEscape(newIp);
    callout += "</strong>";

    const String html = buildEmailHtml(ctx, EMAIL_THEME_INFO, "NEW IP", "The camera changed address", "The ESP32 found the camera at a new IP and already updated the monitored " "endpoint. Update bookmarks and recorders that point to the " "old address.", callout);

    return sendResendEmail(ctx, subject, html);
}

uint16_t allocateLogContextId()
{
    const uint16_t allocated = nextLogContextId;
    nextLogContextId++;

    if (nextLogContextId == 0) {
        nextLogContextId = 1;
    }

    return allocated;
}

}
