#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <MD5Builder.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_bt.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <time.h>

#include "app_config.h"
#if RELEASE_BLUETOOTH_MEMORY
#include <esp32-hal-bt.h>
#endif
#include "dashboard_page.h"
#include "live_player_script.h"
#include "live_stream_relay.h"
#include "secrets.h"

#ifndef CAMERA_ONVIF_USERNAME_VALUE
#define CAMERA_ONVIF_USERNAME_VALUE ""
#endif

#ifndef CAMERA_ONVIF_PASSWORD_VALUE
#define CAMERA_ONVIF_PASSWORD_VALUE ""
#endif

#ifndef CAMERA_RTSP_USERNAME_VALUE
#define CAMERA_RTSP_USERNAME_VALUE CAMERA_ONVIF_USERNAME_VALUE
#endif

#ifndef CAMERA_RTSP_PASSWORD_VALUE
#define CAMERA_RTSP_PASSWORD_VALUE CAMERA_ONVIF_PASSWORD_VALUE
#endif

#ifndef DASHBOARD_ADMIN_PASSWORD_VALUE
#define DASHBOARD_ADMIN_PASSWORD_VALUE ""
#endif

namespace {

constexpr char HOSTNAME[] = "camera-watchdog";
constexpr char FIRMWARE_VERSION[] = "0.8.2";
constexpr char RESEND_API_URL[] = "https://api.resend.com/emails";
constexpr char NVS_NAMESPACE[] = "camwatch";
constexpr char SETTINGS_NVS_NAMESPACE[] = "camcfg";
constexpr char SETTINGS_NVS_KEY[] = "runtime";
constexpr char SETTINGS_DEFAULTS_REVISION_KEY[] = "defaults_rev";
constexpr uint32_t SETTINGS_MAGIC = 0x43573035UL;
constexpr unsigned long ADMIN_RESTART_DELAY_MS = 1200UL;
constexpr unsigned long ONVIF_CATALOG_FALLBACK_RETRY_MS =
    5UL * 60UL * 1000UL;

const IPAddress WS_DISCOVERY_MULTICAST(239, 255, 255, 250);

constexpr char SOAP_GET_SYSTEM_TIME[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">"
    "<s:Body><tds:GetSystemDateAndTime/></s:Body>"
    "</s:Envelope>";

enum class CameraState {
    Starting,
    Discovering,
    Online,
    Degraded,
    Recovering,
    Offline
};

enum class IncidentKind {
    None,
    SnapshotUnavailable,
    CameraOffline
};

enum class AdminAction {
    None,
    HealthCheck,
    Rediscover,
    CameraReboot,
    ResetRebootLimiter,
    RestartEsp32
};

enum class OnvifMetadataStage {
    DeviceInformation,
    Scopes,
    SystemTime,
    EventProperties,
    Complete
};

enum class MediaCanaryOutcome {
    MediaReceived,
    AllProfilesSilentAfterPlay,
    SweepPending,
    Inconclusive
};

enum class HealthEvidenceSource {
    None,
    SnapshotJpeg,
    PreviewJpeg,
    RelayRtp,
    MediaCanaryRtp
};

struct RuntimeSettings {
    char cameraName[CAMERA_NAME_MAX_LENGTH + 1];
    char snapshotProfileMatch[SNAPSHOT_PROFILE_MATCH_MAX_LENGTH + 1];
    uint16_t cameraRtspPort;

    uint32_t healthCheckIntervalMs;
    uint32_t startupGraceMs;
    uint8_t failuresBeforeRediscovery;
    uint8_t failuresBeforeOffline;
    uint8_t successesBeforeRecovery;
    uint8_t videoFailuresBeforeAlert;
    uint8_t videoFailuresBeforeReboot;

    uint8_t enableOnvifAutoReboot;
    uint32_t onvifRebootGraceMs;
    uint32_t onvifRebootCooldownMs;
    uint32_t onvifRebootWindowMs;
    uint8_t maxOnvifRebootAttemptsPerWindow;

    uint32_t wifiConnectTimeoutMs;
    uint8_t enableAutomaticSubnetScan;
    uint32_t discoveryTimeoutMs;
    uint32_t rediscoveryIntervalMs;
    uint32_t subnetScanIntervalMs;

    uint8_t enableEmailAlerts;
    uint8_t emailOnIpChange;
    uint32_t emailRetryIntervalMs;

    uint32_t tcpConnectTimeoutMs;
    uint32_t serviceResponseTimeoutMs;
    uint32_t scanConnectTimeoutMs;
    uint32_t snapshotHttpTimeoutMs;
    uint32_t snapshotResolveRetryIntervalMs;
    uint8_t snapshotFailuresBeforeReresolve;

    uint8_t enableCameraAlertLed;
    uint32_t cameraAlertLedBlinkIntervalMs;
    uint32_t feedMinIntervalMs;
    uint32_t feedTargetCycleMs;
    uint32_t statusRefreshIntervalMs;
};

struct StoredSettingsBlob {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    RuntimeSettings values;
    uint32_t checksum;
};

template <size_t Size>
void copySettingText(char (&destination)[Size], const char* source)
{
    memset(destination, 0, Size);

    if (source == nullptr || Size == 0) {
        return;
    }

    const size_t sourceLength = strlen(source);
    const size_t copyLength = min(sourceLength, Size - 1);
    memcpy(destination, source, copyLength);
    destination[copyLength] = '\0';
}

template <size_t Size>
void copySettingText(char (&destination)[Size], const String& source)
{
    memset(destination, 0, Size);
    const size_t copyLength = min(
        static_cast<size_t>(source.length()),
        Size - 1
    );
    memcpy(destination, source.c_str(), copyLength);
    destination[copyLength] = '\0';
}

RuntimeSettings makeDefaultRuntimeSettings()
{
    RuntimeSettings values{};
    copySettingText(values.cameraName, CAMERA_NAME);
    copySettingText(
        values.snapshotProfileMatch,
        CAMERA_SNAPSHOT_PROFILE_MATCH
    );
    values.cameraRtspPort = CAMERA_RTSP_PORT;

    values.healthCheckIntervalMs = HEALTH_CHECK_INTERVAL_MS;
    values.startupGraceMs = STARTUP_GRACE_MS;
    values.failuresBeforeRediscovery = FAILURES_BEFORE_REDISCOVERY;
    values.failuresBeforeOffline = FAILURES_BEFORE_OFFLINE;
    values.successesBeforeRecovery = SUCCESSES_BEFORE_RECOVERY;
    values.videoFailuresBeforeAlert = VIDEO_FAILURES_BEFORE_ALERT;
    values.videoFailuresBeforeReboot = VIDEO_FAILURES_BEFORE_REBOOT;

    values.enableOnvifAutoReboot = ENABLE_ONVIF_AUTO_REBOOT ? 1 : 0;
    values.onvifRebootGraceMs = ONVIF_REBOOT_GRACE_MS;
    values.onvifRebootCooldownMs = ONVIF_REBOOT_COOLDOWN_MS;
    values.onvifRebootWindowMs = ONVIF_REBOOT_WINDOW_MS;
    values.maxOnvifRebootAttemptsPerWindow =
        MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW;

    values.wifiConnectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS;
    values.enableAutomaticSubnetScan =
        ENABLE_AUTOMATIC_SUBNET_SCAN ? 1 : 0;
    values.discoveryTimeoutMs = DISCOVERY_TIMEOUT_MS;
    values.rediscoveryIntervalMs = REDISCOVERY_INTERVAL_MS;
    values.subnetScanIntervalMs = SUBNET_SCAN_INTERVAL_MS;

    values.enableEmailAlerts = ENABLE_EMAIL_ALERTS ? 1 : 0;
    values.emailOnIpChange = EMAIL_ON_IP_CHANGE ? 1 : 0;
    values.emailRetryIntervalMs = EMAIL_RETRY_INTERVAL_MS;

    values.tcpConnectTimeoutMs = TCP_CONNECT_TIMEOUT_MS;
    values.serviceResponseTimeoutMs = SERVICE_RESPONSE_TIMEOUT_MS;
    values.scanConnectTimeoutMs = SCAN_CONNECT_TIMEOUT_MS;
    values.snapshotHttpTimeoutMs = SNAPSHOT_HTTP_TIMEOUT_MS;
    values.snapshotResolveRetryIntervalMs =
        SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS;
    values.snapshotFailuresBeforeReresolve =
        SNAPSHOT_FAILURES_BEFORE_RERESOLVE;

    values.enableCameraAlertLed = ENABLE_CAMERA_ALERT_LED ? 1 : 0;
    values.cameraAlertLedBlinkIntervalMs =
        CAMERA_ALERT_LED_BLINK_INTERVAL_MS;
    values.feedMinIntervalMs = FEED_MIN_INTERVAL_MS;
    values.feedTargetCycleMs = FEED_TARGET_CYCLE_MS;
    values.statusRefreshIntervalMs = STATUS_REFRESH_INTERVAL_MS;
    return values;
}

RuntimeSettings settings = makeDefaultRuntimeSettings();

static_assert(
    sizeof(RuntimeSettings) <= 256,
    "RuntimeSettings excedeu o limite planejado"
);

#define CAMERA_NAME (settings.cameraName)
#define CAMERA_SNAPSHOT_PROFILE_MATCH (settings.snapshotProfileMatch)
#define CAMERA_RTSP_PORT (settings.cameraRtspPort)
#define HEALTH_CHECK_INTERVAL_MS (settings.healthCheckIntervalMs)
#define STARTUP_GRACE_MS (settings.startupGraceMs)
#define FAILURES_BEFORE_REDISCOVERY (settings.failuresBeforeRediscovery)
#define FAILURES_BEFORE_OFFLINE (settings.failuresBeforeOffline)
#define SUCCESSES_BEFORE_RECOVERY (settings.successesBeforeRecovery)
#define VIDEO_FAILURES_BEFORE_ALERT (settings.videoFailuresBeforeAlert)
#define VIDEO_FAILURES_BEFORE_REBOOT (settings.videoFailuresBeforeReboot)
#define ENABLE_ONVIF_AUTO_REBOOT (settings.enableOnvifAutoReboot != 0)
#define ONVIF_REBOOT_GRACE_MS (settings.onvifRebootGraceMs)
#define ONVIF_REBOOT_COOLDOWN_MS (settings.onvifRebootCooldownMs)
#define ONVIF_REBOOT_WINDOW_MS (settings.onvifRebootWindowMs)
#define MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW \
    (settings.maxOnvifRebootAttemptsPerWindow)
#define WIFI_CONNECT_TIMEOUT_MS (settings.wifiConnectTimeoutMs)
#define ENABLE_AUTOMATIC_SUBNET_SCAN \
    (settings.enableAutomaticSubnetScan != 0)
#define DISCOVERY_TIMEOUT_MS (settings.discoveryTimeoutMs)
#define REDISCOVERY_INTERVAL_MS (settings.rediscoveryIntervalMs)
#define SUBNET_SCAN_INTERVAL_MS (settings.subnetScanIntervalMs)
#define ENABLE_EMAIL_ALERTS (settings.enableEmailAlerts != 0)
#define EMAIL_ON_IP_CHANGE (settings.emailOnIpChange != 0)
#define EMAIL_RETRY_INTERVAL_MS (settings.emailRetryIntervalMs)
#define TCP_CONNECT_TIMEOUT_MS (settings.tcpConnectTimeoutMs)
#define SERVICE_RESPONSE_TIMEOUT_MS (settings.serviceResponseTimeoutMs)
#define SCAN_CONNECT_TIMEOUT_MS (settings.scanConnectTimeoutMs)
#define SNAPSHOT_HTTP_TIMEOUT_MS (settings.snapshotHttpTimeoutMs)
#define SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS \
    (settings.snapshotResolveRetryIntervalMs)
#define SNAPSHOT_FAILURES_BEFORE_RERESOLVE \
    (settings.snapshotFailuresBeforeReresolve)
#define ENABLE_CAMERA_ALERT_LED (settings.enableCameraAlertLed != 0)
#define CAMERA_ALERT_LED_BLINK_INTERVAL_MS \
    (settings.cameraAlertLedBlinkIntervalMs)
#define FEED_MIN_INTERVAL_MS (settings.feedMinIntervalMs)
#define FEED_TARGET_CYCLE_MS (settings.feedTargetCycleMs)
#define STATUS_REFRESH_INTERVAL_MS (settings.statusRefreshIntervalMs)

struct ParsedUrl {
    bool valid = false;
    bool secure = false;
    IPAddress ip;
    uint16_t port = 0;
    String path;
};

struct ParsedHttpResource {
    bool valid = false;
    bool secure = false;
    String host;
    uint16_t port = 0;
    String path;
};

struct CameraTarget {
    bool valid = false;
    IPAddress ip;
    uint16_t onvifPort = 0;
    uint16_t rtspPort = CAMERA_RTSP_PORT;
    String onvifPath;
    String onvifUrl;
    String endpointUuid;
    String scopes;
};

struct OnvifServiceEndpoint {
    bool available = false;
    String url;
    uint16_t majorVersion = 0;
    uint16_t minorVersion = 0;
};

struct OnvifServiceCatalog {
    bool resolved = false;
    bool usedMediaFallback = false;
    bool usernameToken = false;
    bool httpDigest = false;
    bool mediaSnapshotUri = false;
    bool mediaRtpRtspTcp = false;
    bool media2RtspStreaming = false;
    bool eventsPullPoint = false;
    bool ptzStatus = false;
    bool imagingStabilizationAdvertised = false;
    bool analyticsRules = false;
    bool analyticsModules = false;
    uint16_t serviceCount = 0;
    uint32_t attempts = 0;
    uint32_t successes = 0;
    uint32_t failures = 0;
    uint32_t rejectedEndpoints = 0;
    uint32_t lastLatencyMs = 0;
    unsigned long resolvedAtMs = 0;
    int lastStatusCode = 0;
    String lastResult = "Ainda nao consultado";
    OnvifServiceEndpoint device;
    OnvifServiceEndpoint media1;
    OnvifServiceEndpoint media2;
    OnvifServiceEndpoint events;
    OnvifServiceEndpoint ptz;
    OnvifServiceEndpoint imaging;
    OnvifServiceEndpoint analytics;
    OnvifServiceEndpoint deviceIo;
};

struct OnvifDeviceMetadata {
    bool loaded = false;
    bool scopesLoaded = false;
    bool timeLoaded = false;
    bool eventPropertiesLoaded = false;
    bool profileStreaming = false;
    bool profileT = false;
    bool motionEvents = false;
    bool tamperEvents = false;
    bool signalLossEvents = false;
    bool imageQualityEvents = false;
    uint16_t scopeCount = 0;
    uint16_t eventTopicCount = 0;
    uint16_t eventDescriptionCount = 0;
    uint32_t requests = 0;
    uint32_t failures = 0;
    unsigned long loadedAtMs = 0;
    String manufacturer;
    String model;
    String firmware;
    String dateTimeType;
    String timeZone;
    String lastResult = "Ainda nao consultado";
};

struct ServiceProbe {
    bool tcpConnected = false;
    bool protocolResponded = false;
    bool mediaRouteTested = false;
    int statusCode = 0;
    uint32_t latencyMs = 0;
};

struct DiscoveryCandidate {
    CameraTarget target;
    String rawSummary;
};

struct SnapshotTarget {
    bool valid = false;
    String url;
    String profileToken;
    String profileName;
};


struct SnapshotProbe {
    bool attempted = false;
    bool requestAttempted = false;
    bool resolutionAttempted = false;
    bool resolutionTransportFailure = false;
    bool conclusive = false;
    bool targetResolved = false;
    bool httpResponded = false;
    bool jpegValid = false;
    int statusCode = 0;
    uint32_t latencyMs = 0;
    size_t bytesRead = 0;
    String detail = "Ainda nao executado";
};

struct DigestChallenge {
    bool valid = false;
    String realm;
    String nonce;
    String qop;
    String opaque;
    String algorithm = "MD5";
};

WebServer httpServer(HTTP_SERVER_PORT);
WiFiUDP discoveryUdp;
Preferences preferences;
SemaphoreHandle_t cameraIoMutex = nullptr;

bool ensureCameraIoMutex()
{
    if (cameraIoMutex != nullptr) {
        return true;
    }

    cameraIoMutex = xSemaphoreCreateRecursiveMutex();
    return cameraIoMutex != nullptr;
}

class CameraIoGuard {
public:
    explicit CameraIoGuard(TickType_t timeout = 0)
        : acquired_(
            ensureCameraIoMutex() &&
            xSemaphoreTakeRecursive(cameraIoMutex, timeout) == pdTRUE
        )
    {
    }

    ~CameraIoGuard()
    {
        if (acquired_) {
            xSemaphoreGiveRecursive(cameraIoMutex);
        }
    }

    bool acquired() const
    {
        return acquired_;
    }

private:
    bool acquired_ = false;
};

bool settingsLoadedFromNvs = false;
uint32_t settingsRevision = 1;
uint32_t settingsWriteCount = 0;
String settingsWriteToken;
String lastSettingsResult = "Usando valores de fabrica";
AdminAction pendingAdminAction = AdminAction::None;
String lastAdminActionResult = "Nenhuma acao administrativa executada";
unsigned long espRestartRequestedAtMs = 0;

CameraTarget camera;
OnvifServiceCatalog onvifServices;
OnvifDeviceMetadata onvifMetadata;
OnvifMetadataStage onvifMetadataStage =
    OnvifMetadataStage::DeviceInformation;
SnapshotTarget snapshotTarget;
SnapshotProbe lastSnapshotProbe;
LiveMediaProbeResult lastMediaCanaryProbe;
ServiceProbe lastOnvifProbe;
ServiceProbe lastRtspProbe;
CameraState cameraState = CameraState::Starting;
IncidentKind activeIncident = IncidentKind::None;
IncidentKind recoveryIncident = IncidentKind::None;

char discoveryBuffer[WS_DISCOVERY_BUFFER_SIZE + 1];
DiscoveryCandidate discovered[MAX_DISCOVERED_CAMERAS];
size_t discoveredCount = 0;

unsigned long bootAtMs = 0;
unsigned long lastHealthCheckAtMs = 0;
unsigned long lastRediscoveryAtMs = 0;
unsigned long lastSubnetScanAtMs = 0;
unsigned long lastEmailAttemptAtMs = 0;
unsigned long lastStateChangeAtMs = 0;
unsigned long lastCameraAlertLedToggleAtMs = 0;
unsigned long incidentStartedAtMs = 0;
unsigned long recoveryIncidentDurationMs = 0;
unsigned long lastCameraRebootAttemptAtMs = 0;
unsigned long cameraRebootGraceStartedAtMs = 0;
unsigned long cameraRebootWindowStartedAtMs = 0;

uint32_t totalHealthChecks = 0;
uint32_t successfulHealthChecks = 0;
uint32_t failedHealthChecks = 0;
uint32_t inconclusiveHealthChecks = 0;
uint32_t discoveryRuns = 0;
uint32_t subnetScanRuns = 0;
uint32_t emailsSent = 0;
uint32_t emailErrors = 0;
uint32_t snapshotsServed = 0;
uint32_t snapshotErrors = 0;
uint32_t previewCameraFetches = 0;
uint32_t previewBusyResponses = 0;
uint64_t previewBytesRelayed = 0;
uint32_t cameraJpegHttpRequests = 0;
uint32_t snapshotProbeRuns = 0;
uint32_t snapshotProbeFailures = 0;
uint32_t mediaCanaryCycles = 0;
uint32_t mediaCanaryRuns = 0;
uint32_t mediaCanarySuccesses = 0;
uint32_t mediaCanaryFailures = 0;
uint32_t mediaCanaryInconclusive = 0;
uint32_t mediaCanarySweepPending = 0;
uint32_t previewClientAborts = 0;
uint32_t previewHealthEvidenceUses = 0;
uint32_t cameraRebootAttempts = 0;
uint32_t cameraRebootAccepted = 0;
uint32_t cameraRebootErrors = 0;
uint32_t heapBeforeBluetoothReleaseBytes = 0;
uint32_t heapAfterBluetoothReleaseBytes = 0;
uint32_t bluetoothMemoryReleasedBytes = 0;
uint8_t consecutiveFailures = 0;
uint8_t consecutiveSuccesses = 0;
uint8_t consecutiveControlFailures = 0;
uint8_t consecutiveInconclusiveChecks = 0;
uint8_t consecutiveMediaServiceFailures = 0;
uint8_t consecutiveIndependentMediaFailures = 0;
uint8_t snapshotTargetFailureStreak = 0;
uint8_t cameraRebootAttemptsInWindow = 0;
uint8_t onvifMetadataStageFailureStreak = 0;
unsigned long cameraRebootAcceptedAtMs[
    MAX_ONVIF_REBOOT_ATTEMPTS_MAX
] = {0};

bool incidentAlertSent = false;
bool pendingIncidentEmail = false;
bool pendingRecoveryEmail = false;
bool cameraAlertLedOn = false;
bool cameraAlertLedBlinking = false;
bool bluetoothMemoryReleaseAttempted = false;
bool bluetoothStartedBeforeRelease = false;
bool connectedServicesInitialized = false;
bool wifiWasConnected = false;
bool healthCheckInProgress = false;
esp_err_t bluetoothMemoryReleaseResult = ESP_ERR_NOT_SUPPORTED;
esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease =
    ESP_BT_CONTROLLER_STATUS_NUM;

String lastEvent = "Inicializando";
String lastEmailResult = "Nenhum e-mail enviado";
String lastDiscoveryResult = "Ainda nao executada";
String lastScanResult = "Ainda nao executada";
String previousCameraIp;
String lastSnapshotResult = "Ainda nao solicitada";
String lastCameraRebootResult = "Ainda nao solicitado";
String snapshotProfileTokenToSkip;
unsigned long lastSnapshotResolveAttemptAtMs = 0;
unsigned long lastBrowserSnapshotDeliveredAtMs = 0;
unsigned long lastRelayMediaEvidenceConsumedAtMs = 0;
unsigned long lastHealthEvidenceAtMs = 0;
unsigned long lastOnvifMetadataRequestAtMs = 0;
unsigned long lastMediaCanaryAtMs = 0;
size_t lastMediaCanaryProfileIndex = 0;
size_t mediaCanaryNextProfileOffset = 0;
uint8_t lastMediaCanaryProfilesTested = 0;
uint16_t mediaCanarySilentProfilesMask = 0;
unsigned long mediaCanarySilentProfileAtMs[MAX_LIVE_PROFILES] = {0};
static_assert(
    MAX_LIVE_PROFILES <= 16,
    "Mascara do canario RTP suporta no maximo 16 perfis"
);
uint32_t lastBrowserSnapshotBytes = 0;
uint32_t lastBrowserSnapshotLatencyMs = 0;
int lastSnapshotResolveStatusCode = 0;
bool lastSnapshotResolveTransportFailure = false;
bool snapshotProfileFailoverPending = false;
bool lastOnvifMetadataRequestFailed = false;
HealthEvidenceSource lastHealthEvidenceSource = HealthEvidenceSource::None;

void saveCameraTarget();
void invalidateOnvifServiceCatalog();
bool resolveOnvifServiceCatalog(bool forceRefresh = false);
void refreshOnvifDeviceMetadata();
bool requestLiveSynchronizationPoint(
    const char* profileToken,
    String& result
);
void invalidateLiveProfiles();
void clearMediaCanaryFailureEvidence();
void resetMediaCanaryFailureEvidence();
void publishLiveProfilesFromXml(
    const String& profilesXml,
    const String& mediaUrl,
    const IPAddress& cameraIp
);
void appendLiveStatusJson(String& json);
void configureLiveRelay();

uint32_t settingsChecksum(const RuntimeSettings& values)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&values);
    uint32_t hash = 2166136261UL;

    for (size_t index = 0; index < sizeof(RuntimeSettings); index++) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }

    return hash;
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

bool settingTextValid(
    const char* value,
    size_t capacity,
    size_t minimumLength,
    size_t maximumLength
)
{
    const size_t length = boundedTextLength(value, capacity);

    if (
        length >= capacity ||
        length < minimumLength ||
        length > maximumLength
    ) {
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

template <typename T>
bool settingInRange(T value, T minimum, T maximum)
{
    return value >= minimum && value <= maximum;
}

bool validateRuntimeSettings(
    const RuntimeSettings& values,
    String& error
)
{
    if (!settingTextValid(
        values.cameraName,
        sizeof(values.cameraName),
        CAMERA_NAME_MIN_LENGTH,
        CAMERA_NAME_MAX_LENGTH
    )) {
        error = "Nome da camera invalido";
        return false;
    }

    if (!settingTextValid(
        values.snapshotProfileMatch,
        sizeof(values.snapshotProfileMatch),
        0,
        SNAPSHOT_PROFILE_MATCH_MAX_LENGTH
    )) {
        error = "Filtro do perfil ONVIF invalido";
        return false;
    }

#define CHECK_SETTING_RANGE(field, minimum, maximum, message) \
    if (!settingInRange(values.field, minimum, maximum)) { \
        error = message; \
        return false; \
    }

    CHECK_SETTING_RANGE(
        cameraRtspPort,
        CAMERA_RTSP_PORT_MIN,
        CAMERA_RTSP_PORT_MAX,
        "Porta RTSP fora do limite"
    );
    CHECK_SETTING_RANGE(
        healthCheckIntervalMs,
        HEALTH_CHECK_INTERVAL_MIN_MS,
        HEALTH_CHECK_INTERVAL_MAX_MS,
        "Intervalo de verificacao fora do limite"
    );
    CHECK_SETTING_RANGE(
        startupGraceMs,
        STARTUP_GRACE_MIN_MS,
        STARTUP_GRACE_MAX_MS,
        "Tolerancia inicial fora do limite"
    );
    CHECK_SETTING_RANGE(
        failuresBeforeRediscovery,
        FAILURES_BEFORE_REDISCOVERY_MIN,
        FAILURES_BEFORE_REDISCOVERY_MAX,
        "Falhas para redescoberta fora do limite"
    );
    CHECK_SETTING_RANGE(
        failuresBeforeOffline,
        FAILURES_BEFORE_OFFLINE_MIN,
        FAILURES_BEFORE_OFFLINE_MAX,
        "Falhas para offline fora do limite"
    );
    CHECK_SETTING_RANGE(
        successesBeforeRecovery,
        SUCCESSES_BEFORE_RECOVERY_MIN,
        SUCCESSES_BEFORE_RECOVERY_MAX,
        "Sucessos para recuperacao fora do limite"
    );
    CHECK_SETTING_RANGE(
        videoFailuresBeforeAlert,
        VIDEO_FAILURES_BEFORE_ALERT_MIN,
        VIDEO_FAILURES_BEFORE_ALERT_MAX,
        "Falhas de imagem para alerta fora do limite"
    );
    CHECK_SETTING_RANGE(
        videoFailuresBeforeReboot,
        VIDEO_FAILURES_BEFORE_REBOOT_MIN,
        VIDEO_FAILURES_BEFORE_REBOOT_MAX,
        "Falhas de imagem para reboot fora do limite"
    );
    CHECK_SETTING_RANGE(
        onvifRebootGraceMs,
        ONVIF_REBOOT_GRACE_MIN_MS,
        ONVIF_REBOOT_GRACE_MAX_MS,
        "Tolerancia apos reboot fora do limite"
    );
    CHECK_SETTING_RANGE(
        onvifRebootCooldownMs,
        ONVIF_REBOOT_COOLDOWN_MIN_MS,
        ONVIF_REBOOT_COOLDOWN_MAX_MS,
        "Cooldown de reboot fora do limite"
    );
    CHECK_SETTING_RANGE(
        onvifRebootWindowMs,
        ONVIF_REBOOT_WINDOW_MIN_MS,
        ONVIF_REBOOT_WINDOW_MAX_MS,
        "Janela de reboot fora do limite"
    );
    CHECK_SETTING_RANGE(
        maxOnvifRebootAttemptsPerWindow,
        MAX_ONVIF_REBOOT_ATTEMPTS_MIN,
        MAX_ONVIF_REBOOT_ATTEMPTS_MAX,
        "Limite de reboots fora do intervalo"
    );
    CHECK_SETTING_RANGE(
        wifiConnectTimeoutMs,
        WIFI_CONNECT_TIMEOUT_MIN_MS,
        WIFI_CONNECT_TIMEOUT_MAX_MS,
        "Timeout de Wi-Fi fora do limite"
    );
    CHECK_SETTING_RANGE(
        discoveryTimeoutMs,
        DISCOVERY_TIMEOUT_MIN_MS,
        DISCOVERY_TIMEOUT_MAX_MS,
        "Timeout de descoberta fora do limite"
    );
    CHECK_SETTING_RANGE(
        rediscoveryIntervalMs,
        REDISCOVERY_INTERVAL_MIN_MS,
        REDISCOVERY_INTERVAL_MAX_MS,
        "Intervalo de redescoberta fora do limite"
    );
    CHECK_SETTING_RANGE(
        subnetScanIntervalMs,
        SUBNET_SCAN_INTERVAL_MIN_MS,
        SUBNET_SCAN_INTERVAL_MAX_MS,
        "Intervalo de varredura fora do limite"
    );
    CHECK_SETTING_RANGE(
        emailRetryIntervalMs,
        EMAIL_RETRY_INTERVAL_MIN_MS,
        EMAIL_RETRY_INTERVAL_MAX_MS,
        "Intervalo de e-mail fora do limite"
    );
    CHECK_SETTING_RANGE(
        tcpConnectTimeoutMs,
        TCP_CONNECT_TIMEOUT_MIN_MS,
        TCP_CONNECT_TIMEOUT_MAX_MS,
        "Timeout TCP fora do limite"
    );
    CHECK_SETTING_RANGE(
        serviceResponseTimeoutMs,
        SERVICE_RESPONSE_TIMEOUT_MIN_MS,
        SERVICE_RESPONSE_TIMEOUT_MAX_MS,
        "Timeout de protocolo fora do limite"
    );
    CHECK_SETTING_RANGE(
        scanConnectTimeoutMs,
        SCAN_CONNECT_TIMEOUT_MIN_MS,
        SCAN_CONNECT_TIMEOUT_MAX_MS,
        "Timeout da varredura fora do limite"
    );
    CHECK_SETTING_RANGE(
        snapshotHttpTimeoutMs,
        SNAPSHOT_HTTP_TIMEOUT_MIN_MS,
        SNAPSHOT_HTTP_TIMEOUT_MAX_MS,
        "Timeout do JPEG fora do limite"
    );
    CHECK_SETTING_RANGE(
        snapshotResolveRetryIntervalMs,
        SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS,
        SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS,
        "Intervalo para resolver snapshot fora do limite"
    );
    CHECK_SETTING_RANGE(
        snapshotFailuresBeforeReresolve,
        SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN,
        SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX,
        "Falhas para resolver novamente o snapshot fora do limite"
    );
    CHECK_SETTING_RANGE(
        cameraAlertLedBlinkIntervalMs,
        CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS,
        CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS,
        "Intervalo do LED fora do limite"
    );
    CHECK_SETTING_RANGE(
        feedMinIntervalMs,
        FEED_MIN_INTERVAL_MIN_MS,
        FEED_MIN_INTERVAL_MAX_MS,
        "Intervalo minimo da previa fora do limite"
    );
    CHECK_SETTING_RANGE(
        feedTargetCycleMs,
        FEED_TARGET_CYCLE_MIN_MS,
        FEED_TARGET_CYCLE_MAX_MS,
        "Ciclo da previa fora do limite"
    );
    CHECK_SETTING_RANGE(
        statusRefreshIntervalMs,
        STATUS_REFRESH_INTERVAL_MIN_MS,
        STATUS_REFRESH_INTERVAL_MAX_MS,
        "Atualizacao do painel fora do limite"
    );

#undef CHECK_SETTING_RANGE

    if (
        values.enableOnvifAutoReboot > 1 ||
        values.enableAutomaticSubnetScan > 1 ||
        values.enableEmailAlerts > 1 ||
        values.emailOnIpChange > 1 ||
        values.enableCameraAlertLed > 1
    ) {
        error = "Opcao booleana invalida";
        return false;
    }

    if (values.failuresBeforeOffline < values.failuresBeforeRediscovery) {
        error = "Offline nao pode ocorrer antes da tentativa de redescoberta";
        return false;
    }

    if (values.videoFailuresBeforeReboot < values.videoFailuresBeforeAlert) {
        error = "O reboot nao pode ocorrer antes do alerta de imagem";
        return false;
    }

    if (values.onvifRebootWindowMs < values.onvifRebootCooldownMs) {
        error = "A janela de reboots deve ser maior ou igual ao cooldown";
        return false;
    }

    if (values.feedTargetCycleMs < values.feedMinIntervalMs) {
        error = "O ciclo da previa deve ser maior ou igual ao intervalo minimo";
        return false;
    }

    return true;
}

bool migrateLegacyConnectivityDefaults(RuntimeSettings& values)
{
    const RuntimeSettings defaults = makeDefaultRuntimeSettings();
    bool changed = false;

    if (strcmp(values.snapshotProfileMatch, "sub") == 0) {
        copySettingText(
            values.snapshotProfileMatch,
            defaults.snapshotProfileMatch
        );
        changed = true;
    }

#define MIGRATE_LEGACY_DEFAULT(field, legacyValue) \
    if (values.field == legacyValue) { \
        values.field = defaults.field; \
        changed = true; \
    }

    MIGRATE_LEGACY_DEFAULT(healthCheckIntervalMs, 30000UL);
    MIGRATE_LEGACY_DEFAULT(startupGraceMs, 45000UL);
    MIGRATE_LEGACY_DEFAULT(videoFailuresBeforeAlert, 3);
    MIGRATE_LEGACY_DEFAULT(onvifRebootGraceMs, 90000UL);
    MIGRATE_LEGACY_DEFAULT(onvifRebootCooldownMs, 15UL * 60UL * 1000UL);
    MIGRATE_LEGACY_DEFAULT(onvifRebootWindowMs, 24UL * 60UL * 60UL * 1000UL);
    MIGRATE_LEGACY_DEFAULT(maxOnvifRebootAttemptsPerWindow, 3);
    MIGRATE_LEGACY_DEFAULT(rediscoveryIntervalMs, 60000UL);
    MIGRATE_LEGACY_DEFAULT(serviceResponseTimeoutMs, 1500UL);
    MIGRATE_LEGACY_DEFAULT(snapshotHttpTimeoutMs, 7000UL);
    MIGRATE_LEGACY_DEFAULT(snapshotResolveRetryIntervalMs, 30000UL);
    MIGRATE_LEGACY_DEFAULT(feedMinIntervalMs, 1000UL);
    MIGRATE_LEGACY_DEFAULT(feedTargetCycleMs, 1000UL);
    MIGRATE_LEGACY_DEFAULT(feedMinIntervalMs, 2000UL);
    MIGRATE_LEGACY_DEFAULT(feedTargetCycleMs, 2000UL);

#undef MIGRATE_LEGACY_DEFAULT

    return changed;
}

bool saveRuntimeSettingsToNvs(const RuntimeSettings& values)
{
    StoredSettingsBlob blob{};
    blob.magic = SETTINGS_MAGIC;
    blob.schemaVersion = RUNTIME_SETTINGS_SCHEMA_VERSION;
    blob.payloadSize = sizeof(RuntimeSettings);
    blob.values = values;
    blob.checksum = settingsChecksum(blob.values);

    if (!preferences.begin(SETTINGS_NVS_NAMESPACE, false)) {
        return false;
    }

    const size_t bytesWritten = preferences.putBytes(
        SETTINGS_NVS_KEY,
        &blob,
        sizeof(blob)
    );
    const size_t revisionBytesWritten = bytesWritten == sizeof(blob)
        ? preferences.putUShort(
            SETTINGS_DEFAULTS_REVISION_KEY,
            RUNTIME_SETTINGS_DEFAULTS_REVISION
        )
        : 0;
    preferences.end();
    return
        bytesWritten == sizeof(blob) &&
        revisionBytesWritten == sizeof(uint16_t);
}

bool clearRuntimeSettingsNvs()
{
    if (!preferences.begin(SETTINGS_NVS_NAMESPACE, false)) {
        return false;
    }

    const bool cleared = preferences.clear();
    preferences.end();
    return cleared;
}

void loadRuntimeSettings()
{
    settings = makeDefaultRuntimeSettings();
    settingsLoadedFromNvs = false;

    if (!preferences.begin(SETTINGS_NVS_NAMESPACE, true)) {
        lastSettingsResult = "NVS indisponivel; usando valores de fabrica";
        Serial.println(lastSettingsResult);
        return;
    }

    const size_t storedSize = preferences.getBytesLength(SETTINGS_NVS_KEY);
    StoredSettingsBlob blob{};
    const size_t bytesRead = storedSize == sizeof(blob)
        ? preferences.getBytes(SETTINGS_NVS_KEY, &blob, sizeof(blob))
        : 0;
    const uint16_t storedDefaultsRevision = preferences.getUShort(
        SETTINGS_DEFAULTS_REVISION_KEY,
        0
    );
    preferences.end();

    const bool storedBlobValid =
        bytesRead == sizeof(blob) &&
        blob.magic == SETTINGS_MAGIC &&
        blob.schemaVersion == RUNTIME_SETTINGS_SCHEMA_VERSION &&
        blob.payloadSize == sizeof(RuntimeSettings) &&
        blob.checksum == settingsChecksum(blob.values);

    if (!storedBlobValid) {
        lastSettingsResult = storedSize == 0
            ? "Usando valores de fabrica"
            : "Configuracao salva invalida; valores de fabrica restaurados";
        Serial.println(lastSettingsResult);
        return;
    }

    const bool defaultsRevisionPending =
        storedDefaultsRevision < RUNTIME_SETTINGS_DEFAULTS_REVISION;
    const bool connectivityDefaultsMigrated =
        defaultsRevisionPending &&
        migrateLegacyConnectivityDefaults(blob.values);
    bool previewLimitMigrated = false;

    if (blob.values.feedMinIntervalMs < FEED_MIN_INTERVAL_MIN_MS) {
        blob.values.feedMinIntervalMs = FEED_MIN_INTERVAL_MIN_MS;
        previewLimitMigrated = true;
    }

    if (blob.values.feedTargetCycleMs < FEED_TARGET_CYCLE_MIN_MS) {
        blob.values.feedTargetCycleMs = FEED_TARGET_CYCLE_MIN_MS;
        previewLimitMigrated = true;
    }

    if (blob.values.feedTargetCycleMs < blob.values.feedMinIntervalMs) {
        blob.values.feedTargetCycleMs = blob.values.feedMinIntervalMs;
        previewLimitMigrated = true;
    }

    String validationError;

    if (!validateRuntimeSettings(blob.values, validationError)) {
        lastSettingsResult =
            "Configuracao salva invalida; valores de fabrica restaurados";
        Serial.printf(
            "%s: %s.\n",
            lastSettingsResult.c_str(),
            validationError.c_str()
        );
        return;
    }

    settings = blob.values;
    settingsLoadedFromNvs = true;

    if (defaultsRevisionPending || previewLimitMigrated) {
        const bool migratedSaved = saveRuntimeSettingsToNvs(settings);

        if (connectivityDefaultsMigrated) {
            lastSettingsResult = migratedSaved
                ? "Configuracoes carregadas; novos padroes de conectividade aplicados"
                : "Configuracoes carregadas; novos padroes nao persistidos";
        } else if (previewLimitMigrated) {
            lastSettingsResult = migratedSaved
                ? "Configuracoes carregadas; limite da previa ajustado"
                : "Configuracoes carregadas; ajuste da previa nao persistido";
        } else {
            lastSettingsResult = migratedSaved
                ? "Configuracoes carregadas; revisao de padroes registrada"
                : "Configuracoes carregadas; revisao de padroes nao persistida";
        }
    } else {
        lastSettingsResult = "Configuracoes carregadas da NVS";
    }

    Serial.println(lastSettingsResult);
    Serial.printf(
        "Configuracoes NVS: ciclo=%lus; alerta=%u; reboot=%u; auto=%s.\n",
        static_cast<unsigned long>(HEALTH_CHECK_INTERVAL_MS / 1000UL),
        VIDEO_FAILURES_BEFORE_ALERT,
        VIDEO_FAILURES_BEFORE_REBOOT,
        ENABLE_ONVIF_AUTO_REBOOT ? "ativo" : "desativado"
    );
}

String makeSettingsWriteToken()
{
    char buffer[41];
    snprintf(
        buffer,
        sizeof(buffer),
        "%08lx%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random())
    );
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
    uint8_t difference = static_cast<uint8_t>(
        suppliedLength == expectedLength ? 0 : 1
    );

    for (size_t index = 0; index < expectedLength; index++) {
        const uint8_t suppliedByte = index < suppliedLength
            ? static_cast<uint8_t>(supplied.charAt(index))
            : 0;
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
        case AdminAction::CameraReboot:
            return "camera_reboot";
        case AdminAction::ResetRebootLimiter:
            return "reset_reboot_limiter";
        case AdminAction::RestartEsp32:
            return "restart_esp32";
    }

    return "unknown";
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
        case HealthEvidenceSource::RelayRtp:
            return "relay_rtp";
        case HealthEvidenceSource::MediaCanaryRtp:
            return "media_canary_rtp";
    }

    return "unknown";
}

void beginOrEscalateIncident(IncidentKind kind)
{
    if (kind == IncidentKind::None) {
        return;
    }

    if (activeIncident == IncidentKind::None) {
        activeIncident = kind;
        consecutiveSuccesses = 0;
        incidentStartedAtMs = millis();
        incidentAlertSent = false;
        pendingIncidentEmail = false;
        return;
    }

    if (
        activeIncident == IncidentKind::SnapshotUnavailable &&
        kind == IncidentKind::CameraOffline
    ) {
        activeIncident = kind;
        consecutiveSuccesses = 0;

        if (incidentAlertSent) {
            incidentAlertSent = false;
            pendingIncidentEmail = true;
        }

        return;
    }

    if (
        activeIncident == IncidentKind::CameraOffline &&
        kind == IncidentKind::SnapshotUnavailable &&
        !incidentAlertSent
    ) {
        activeIncident = kind;
        consecutiveSuccesses = 0;
    }
}

void queueIncidentAlert()
{
    if (
        ENABLE_EMAIL_ALERTS &&
        activeIncident != IncidentKind::None &&
        !incidentAlertSent
    ) {
        pendingIncidentEmail = true;
    }
}

void resolveActiveIncident()
{
    if (activeIncident == IncidentKind::None) {
        return;
    }

    if (incidentAlertSent && ENABLE_EMAIL_ALERTS) {
        recoveryIncident = activeIncident;
        recoveryIncidentDurationMs = millis() - incidentStartedAtMs;
        pendingRecoveryEmail = true;
    }

    pendingIncidentEmail = false;
    incidentAlertSent = false;
    activeIncident = IncidentKind::None;
    incidentStartedAtMs = 0;
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

    if (!ENABLE_CAMERA_ALERT_LED) {
        cameraAlertLedBlinking = false;
        lastCameraAlertLedToggleAtMs = now;

        if (cameraAlertLedOn) {
            writeCameraAlertLed(false);
        }

        return;
    }

    const bool shouldBlink =
        WiFi.status() != WL_CONNECTED ||
        cameraState == CameraState::Offline ||
        cameraState == CameraState::Recovering ||
        activeIncident != IncidentKind::None;

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

    if (
        now - lastCameraAlertLedToggleAtMs >=
        CAMERA_ALERT_LED_BLINK_INTERVAL_MS
    ) {
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
    bluetoothControllerStatusBeforeRelease =
        esp_bt_controller_get_status();

    bluetoothStartedBeforeRelease = btStarted();
    bluetoothMemoryReleaseResult = esp_bt_mem_release(ESP_BT_MODE_BTDM);
    heapAfterBluetoothReleaseBytes = ESP.getFreeHeap();

    if (heapAfterBluetoothReleaseBytes > heapBeforeBluetoothReleaseBytes) {
        bluetoothMemoryReleasedBytes =
            heapAfterBluetoothReleaseBytes -
            heapBeforeBluetoothReleaseBytes;
    }

    if (bluetoothMemoryReleaseResult == ESP_OK) {
        Serial.printf(
            "Memoria Bluetooth liberada: %u bytes.\n",
            bluetoothMemoryReleasedBytes
        );
    } else if (bluetoothMemoryReleaseResult == ESP_ERR_NOT_FOUND) {
        Serial.println(
            "Memoria Bluetooth ja estava liberada ou nao foi reservada."
        );
    } else {
        Serial.printf(
            "Nao foi possivel liberar memoria Bluetooth: %d\n",
            static_cast<int>(bluetoothMemoryReleaseResult)
        );
    }

    Serial.printf(
        "Heap Bluetooth: antes=%u depois=%u diferenca=%u bytes\n",
        heapBeforeBluetoothReleaseBytes,
        heapAfterBluetoothReleaseBytes,
        bluetoothMemoryReleasedBytes
    );
#else
    Serial.println("Liberacao da memoria Bluetooth desativada.");
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

bool cameraCredentialsConfigured()
{
    return strlen(CAMERA_ONVIF_USERNAME_VALUE) > 0;
}

bool clockSynchronized()
{
    return time(nullptr) >= 1700000000;
}

String makeWsseSecurityHeader()
{
    if (!cameraCredentialsConfigured() || !clockSynchronized()) {
        return String();
    }

    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));

    const String created = currentTimestamp();
    uint8_t digest[20];
    mbedtls_sha1_context context;
    mbedtls_sha1_init(&context);

    bool digestOk = mbedtls_sha1_starts_ret(&context) == 0;
    digestOk = digestOk &&
        mbedtls_sha1_update_ret(&context, nonce, sizeof(nonce)) == 0;
    digestOk = digestOk &&
        mbedtls_sha1_update_ret(
            &context,
            reinterpret_cast<const unsigned char*>(created.c_str()),
            created.length()
        ) == 0;
    digestOk = digestOk &&
        mbedtls_sha1_update_ret(
            &context,
            reinterpret_cast<const unsigned char*>(CAMERA_ONVIF_PASSWORD_VALUE),
            strlen(CAMERA_ONVIF_PASSWORD_VALUE)
        ) == 0;
    digestOk = digestOk && mbedtls_sha1_finish_ret(&context, digest) == 0;
    mbedtls_sha1_free(&context);

    if (!digestOk) {
        return String();
    }

    unsigned char nonceBase64[32];
    unsigned char digestBase64[40];
    size_t nonceLength = 0;
    size_t digestLength = 0;

    if (
        mbedtls_base64_encode(
            nonceBase64,
            sizeof(nonceBase64) - 1,
            &nonceLength,
            nonce,
            sizeof(nonce)
        ) != 0 ||
        mbedtls_base64_encode(
            digestBase64,
            sizeof(digestBase64) - 1,
            &digestLength,
            digest,
            sizeof(digest)
        ) != 0
    ) {
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
    header += htmlEscape(CAMERA_ONVIF_USERNAME_VALUE);
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

bool cameraRebootGraceActive()
{
    return
        cameraRebootGraceStartedAtMs != 0 &&
        millis() - cameraRebootGraceStartedAtMs < ONVIF_REBOOT_GRACE_MS;
}

unsigned long cameraRebootGraceRemainingMs()
{
    if (!cameraRebootGraceActive()) {
        return 0;
    }

    return ONVIF_REBOOT_GRACE_MS -
        (millis() - cameraRebootGraceStartedAtMs);
}

void refreshCameraRebootWindow()
{
    const unsigned long now = millis();
    uint8_t activeCount = 0;

    for (
        uint8_t index = 0;
        index < cameraRebootAttemptsInWindow;
        index++
    ) {
        const unsigned long acceptedAtMs = cameraRebootAcceptedAtMs[index];

        if (
            acceptedAtMs != 0 &&
            now - acceptedAtMs < ONVIF_REBOOT_WINDOW_MS
        ) {
            cameraRebootAcceptedAtMs[activeCount++] = acceptedAtMs;
        }
    }

    for (
        uint8_t index = activeCount;
        index < MAX_ONVIF_REBOOT_ATTEMPTS_MAX;
        index++
    ) {
        cameraRebootAcceptedAtMs[index] = 0;
    }

    cameraRebootAttemptsInWindow = activeCount;
    cameraRebootWindowStartedAtMs = activeCount > 0
        ? cameraRebootAcceptedAtMs[0]
        : 0;
}

String durationText(unsigned long durationMs)
{
    const unsigned long totalSeconds = durationMs / 1000UL;
    const unsigned long hours = totalSeconds / 3600UL;
    const unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    const unsigned long seconds = totalSeconds % 60UL;

    char buffer[48];
    snprintf(
        buffer,
        sizeof(buffer),
        "%luh %lum %lus",
        hours,
        minutes,
        seconds
    );

    return String(buffer);
}

uint32_t ipToUint32(const IPAddress& ip)
{
    return
        (static_cast<uint32_t>(ip[0]) << 24) |
        (static_cast<uint32_t>(ip[1]) << 16) |
        (static_cast<uint32_t>(ip[2]) << 8) |
        static_cast<uint32_t>(ip[3]);
}

IPAddress uint32ToIp(uint32_t value)
{
    return IPAddress(
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF)
    );
}

bool parseHttpResourceUrl(
    const String& url,
    ParsedHttpResource& parsed
)
{
    parsed = ParsedHttpResource{};

    String normalizedUrl = url;
    normalizedUrl.trim();

    int hostStart = -1;

    if (normalizedUrl.startsWith("http://")) {
        parsed.secure = false;
        parsed.port = 80;
        hostStart = 7;
    } else if (normalizedUrl.startsWith("https://")) {
        parsed.secure = true;
        parsed.port = 443;
        hostStart = 8;
    } else {
        return false;
    }

    const int pathStart = normalizedUrl.indexOf('/', hostStart);
    const int hostEnd =
        pathStart >= 0 ? pathStart : normalizedUrl.length();
    String authority = normalizedUrl.substring(hostStart, hostEnd);
    parsed.path =
        pathStart >= 0 ? normalizedUrl.substring(pathStart) : "/";

    if (
        authority.isEmpty() ||
        authority.indexOf('@') >= 0 ||
        parsed.path.indexOf('\r') >= 0 ||
        parsed.path.indexOf('\n') >= 0
    ) {
        return false;
    }

    const int colonPosition = authority.lastIndexOf(':');

    if (colonPosition > 0) {
        const String portText = authority.substring(colonPosition + 1);

        if (portText.isEmpty()) {
            return false;
        }

        for (size_t index = 0; index < portText.length(); index++) {
            if (!isdigit(static_cast<unsigned char>(portText.charAt(index)))) {
                return false;
            }
        }

        const long parsedPort = portText.toInt();

        if (parsedPort <= 0 || parsedPort > 65535) {
            return false;
        }

        parsed.port = static_cast<uint16_t>(parsedPort);
        authority = authority.substring(0, colonPosition);
    }

    if (authority.isEmpty()) {
        return false;
    }

    parsed.host = authority;
    parsed.valid = true;
    return true;
}

bool parseHttpUrl(const String& url, ParsedUrl& parsed)
{
    parsed = ParsedUrl{};
    ParsedHttpResource resource;

    if (!parseHttpResourceUrl(url, resource)) {
        return false;
    }

    if (!parsed.ip.fromString(resource.host.c_str())) {
        return false;
    }

    parsed.valid = true;
    parsed.secure = resource.secure;
    parsed.port = resource.port;
    parsed.path = resource.path;
    return true;
}

String extractDigestParameter(const String& challenge, const char* name)
{
    String normalized = challenge;
    normalized.toLowerCase();

    String marker = String(name);
    marker.toLowerCase();
    marker += '=';

    const int markerPosition = normalized.indexOf(marker);

    if (markerPosition < 0) {
        return String();
    }

    int valueStart = markerPosition + marker.length();

    while (
        valueStart < static_cast<int>(challenge.length()) &&
        isspace(static_cast<unsigned char>(challenge.charAt(valueStart)))
    ) {
        valueStart++;
    }

    if (valueStart >= static_cast<int>(challenge.length())) {
        return String();
    }

    if (challenge.charAt(valueStart) == '"') {
        valueStart++;
        const int valueEnd = challenge.indexOf('"', valueStart);
        return valueEnd >= 0
            ? challenge.substring(valueStart, valueEnd)
            : String();
    }

    int valueEnd = valueStart;

    while (
        valueEnd < static_cast<int>(challenge.length()) &&
        challenge.charAt(valueEnd) != ',' &&
        !isspace(static_cast<unsigned char>(challenge.charAt(valueEnd)))
    ) {
        valueEnd++;
    }

    String value = challenge.substring(valueStart, valueEnd);
    value.trim();
    return value;
}

bool digestQopSupportsAuth(const String& qopList)
{
    int tokenStart = 0;

    while (tokenStart < static_cast<int>(qopList.length())) {
        const int commaPosition = qopList.indexOf(',', tokenStart);
        const int tokenEnd = commaPosition >= 0
            ? commaPosition
            : static_cast<int>(qopList.length());

        String token = qopList.substring(tokenStart, tokenEnd);
        token.trim();
        token.toLowerCase();

        if (token == "auth") {
            return true;
        }

        if (commaPosition < 0) {
            break;
        }

        tokenStart = commaPosition + 1;
    }

    return false;
}

bool parseDigestChallenge(
    const String& authenticateHeader,
    DigestChallenge& challenge
)
{
    challenge = DigestChallenge{};

    String normalized = authenticateHeader;
    normalized.toLowerCase();
    const int digestPosition = normalized.indexOf("digest");

    if (digestPosition < 0) {
        return false;
    }

    const String digestText = authenticateHeader.substring(digestPosition + 6);
    challenge.realm = extractDigestParameter(digestText, "realm");
    challenge.nonce = extractDigestParameter(digestText, "nonce");
    challenge.qop = extractDigestParameter(digestText, "qop");
    challenge.opaque = extractDigestParameter(digestText, "opaque");

    const String algorithm = extractDigestParameter(digestText, "algorithm");

    if (!algorithm.isEmpty()) {
        challenge.algorithm = algorithm;
    }

    challenge.valid = !challenge.realm.isEmpty() && !challenge.nonce.isEmpty();
    return challenge.valid;
}

String md5Hex(const String& input)
{
    MD5Builder md5;
    md5.begin();
    md5.add(input);
    md5.calculate();
    return md5.toString();
}

String httpQuotedEscape(const String& input)
{
    String escaped;
    escaped.reserve(input.length() + 8);

    for (size_t index = 0; index < input.length(); index++) {
        const char value = input.charAt(index);

        if (value == '\\' || value == '"') {
            escaped += '\\';
        }

        escaped += value;
    }

    return escaped;
}

String makeDigestAuthorization(
    const char* method,
    const String& url,
    const DigestChallenge& challenge
)
{
    if (!cameraCredentialsConfigured() || !challenge.valid) {
        return String();
    }

    ParsedHttpResource resource;

    if (!parseHttpResourceUrl(url, resource)) {
        return String();
    }

    char cnonceBuffer[33];
    snprintf(
        cnonceBuffer,
        sizeof(cnonceBuffer),
        "%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random())
    );

    const String cnonce = cnonceBuffer;
    const String nonceCount = "00000001";

    String ha1 = md5Hex(
        String(CAMERA_ONVIF_USERNAME_VALUE) + ':' +
        challenge.realm + ':' + CAMERA_ONVIF_PASSWORD_VALUE
    );

    String normalizedAlgorithm = challenge.algorithm;
    normalizedAlgorithm.toLowerCase();

    if (normalizedAlgorithm == "md5-sess") {
        ha1 = md5Hex(ha1 + ':' + challenge.nonce + ':' + cnonce);
    } else if (normalizedAlgorithm != "md5" && !normalizedAlgorithm.isEmpty()) {
        return String();
    }

    const String ha2 = md5Hex(String(method) + ':' + resource.path);
    String selectedQop;

    if (!challenge.qop.isEmpty()) {
        if (!digestQopSupportsAuth(challenge.qop)) {
            return String();
        }

        selectedQop = "auth";
    }

    const String response = selectedQop.isEmpty()
        ? md5Hex(ha1 + ':' + challenge.nonce + ':' + ha2)
        : md5Hex(
            ha1 + ':' + challenge.nonce + ':' + nonceCount + ':' +
            cnonce + ':' + selectedQop + ':' + ha2
        );

    String authorization;
    authorization.reserve(512);
    authorization += "username=\"";
    authorization += httpQuotedEscape(CAMERA_ONVIF_USERNAME_VALUE);
    authorization += "\", realm=\"";
    authorization += httpQuotedEscape(challenge.realm);
    authorization += "\", nonce=\"";
    authorization += httpQuotedEscape(challenge.nonce);
    authorization += "\", uri=\"";
    authorization += httpQuotedEscape(resource.path);
    authorization += "\", response=\"";
    authorization += response;
    authorization += '"';

    if (!challenge.algorithm.isEmpty()) {
        authorization += ", algorithm=";
        authorization += challenge.algorithm;
    }

    if (!selectedQop.isEmpty()) {
        authorization += ", qop=";
        authorization += selectedQop;
        authorization += ", nc=";
        authorization += nonceCount;
        authorization += ", cnonce=\"";
        authorization += cnonce;
        authorization += '"';
    }

    if (!challenge.opaque.isEmpty()) {
        authorization += ", opaque=\"";
        authorization += httpQuotedEscape(challenge.opaque);
        authorization += '"';
    }

    return authorization;
}

void applyCameraAuthorization(
    HTTPClient& request,
    const String& digestAuthorization
)
{
    if (!digestAuthorization.isEmpty()) {
        request.setAuthorizationType("Digest");
        request.setAuthorization(digestAuthorization.c_str());
        return;
    }

    if (cameraCredentialsConfigured()) {
        request.setAuthorizationType("Basic");
        request.setAuthorization(CAMERA_ONVIF_USERNAME_VALUE, CAMERA_ONVIF_PASSWORD_VALUE);
    }
}

int openAuthenticatedCameraGet(
    HTTPClient& request,
    WiFiClient& client,
    const String& url
)
{
    const char* responseHeaders[] = {
        "Content-Type",
        "WWW-Authenticate"
    };

    request.setConnectTimeout(TCP_CONNECT_TIMEOUT_MS);
    request.setTimeout(SNAPSHOT_HTTP_TIMEOUT_MS);
    request.setReuse(false);
    request.useHTTP10(true);
    request.setUserAgent(
        String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION
    );
    request.collectHeaders(responseHeaders, 2);

    const auto executeAttempt = [&](const String& digestAuthorization) {
        if (!request.begin(client, url)) {
            return -1;
        }

        cameraJpegHttpRequests++;
        request.addHeader("Accept", "image/jpeg, image/*;q=0.9, */*;q=0.1");
        applyCameraAuthorization(request, digestAuthorization);
        return request.GET();
    };

    int statusCode = executeAttempt(String());

    if (statusCode != 401 || !cameraCredentialsConfigured()) {
        return statusCode;
    }

    DigestChallenge challenge;
    const String authenticateHeader = request.header("WWW-Authenticate");

    if (!parseDigestChallenge(authenticateHeader, challenge)) {
        return statusCode;
    }

    const String digestAuthorization = makeDigestAuthorization(
        "GET",
        url,
        challenge
    );

    if (digestAuthorization.isEmpty()) {
        return statusCode;
    }

    request.end();
    client.stop();
    return executeAttempt(digestAuthorization);
}

String makeOnvifUrl(
    const IPAddress& ip,
    uint16_t port,
    const String& path
)
{
    String url = "http://";
    url += ip.toString();

    if (port != 80) {
        url += ':';
        url += port;
    }

    if (!path.startsWith("/")) {
        url += '/';
    }

    url += path;
    return url;
}

bool extractLocalTag(
    const String& xml,
    const char* localName,
    int fromPosition,
    String& value,
    int& nextPosition
)
{
    const String marker = String(localName) + ">";
    int searchPosition = fromPosition;

    while (true) {
        const int namePosition = xml.indexOf(marker, searchPosition);

        if (namePosition < 0) {
            return false;
        }

        const int openStart = xml.lastIndexOf('<', namePosition);

        if (openStart < 0) {
            return false;
        }

        const char beforeLocalName =
            namePosition > openStart
                ? xml.charAt(namePosition - 1)
                : '\0';

        if (beforeLocalName != '<' && beforeLocalName != ':') {
            searchPosition = namePosition + marker.length();
            continue;
        }

        if (
            openStart + 1 < static_cast<int>(xml.length()) &&
            xml.charAt(openStart + 1) == '/'
        ) {
            searchPosition = namePosition + marker.length();
            continue;
        }

        const int contentStart = namePosition + marker.length();
        const int closeStart = xml.indexOf("</", contentStart);

        if (closeStart < 0) {
            return false;
        }

        const int closeEnd = xml.indexOf('>', closeStart + 2);

        if (closeEnd < 0) {
            return false;
        }

        const String closingTag = xml.substring(closeStart, closeEnd + 1);

        if (closingTag.indexOf(localName) < 0) {
            searchPosition = contentStart;
            continue;
        }

        value = xml.substring(contentStart, closeStart);
        value.trim();
        nextPosition = closeEnd + 1;
        return true;
    }
}

String firstWhitespaceToken(const String& text)
{
    int start = 0;

    while (
        start < static_cast<int>(text.length()) &&
        isspace(static_cast<unsigned char>(text.charAt(start)))
    ) {
        start++;
    }

    int end = start;

    while (
        end < static_cast<int>(text.length()) &&
        !isspace(static_cast<unsigned char>(text.charAt(end)))
    ) {
        end++;
    }

    return text.substring(start, end);
}

bool containsIgnoreCase(const String& haystack, const String& needle)
{
    if (needle.isEmpty()) {
        return true;
    }

    String normalizedHaystack = haystack;
    String normalizedNeedle = needle;
    normalizedHaystack.toLowerCase();
    normalizedNeedle.toLowerCase();

    return normalizedHaystack.indexOf(normalizedNeedle) >= 0;
}

String xmlUnescape(String value)
{
    value.replace("&amp;", "&");
    value.replace("&quot;", "\"");
    value.replace("&apos;", "'");
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    return value;
}

bool extractXmlAttribute(
    const String& openingTag,
    const char* attributeName,
    String& value
)
{
    const String doubleQuoteMarker = String(attributeName) + "=\"";
    int valueStart = openingTag.indexOf(doubleQuoteMarker);
    char closingQuote = '\"';

    if (valueStart >= 0) {
        valueStart += doubleQuoteMarker.length();
    } else {
        const String singleQuoteMarker = String(attributeName) + "='";
        valueStart = openingTag.indexOf(singleQuoteMarker);

        if (valueStart < 0) {
            return false;
        }

        valueStart += singleQuoteMarker.length();
        closingQuote = '\'';
    }

    const int valueEnd = openingTag.indexOf(closingQuote, valueStart);

    if (valueEnd < 0) {
        return false;
    }

    value = xmlUnescape(openingTag.substring(valueStart, valueEnd));
    return true;
}

bool findNextOpeningTag(
    const String& xml,
    const char* localName,
    int fromPosition,
    int& openingStart,
    int& openingEnd
)
{
    int searchPosition = fromPosition;
    const size_t localNameLength = strlen(localName);

    while (true) {
        const int namePosition = xml.indexOf(localName, searchPosition);

        if (namePosition < 0) {
            return false;
        }

        const int nameEnd = namePosition + localNameLength;
        const char before =
            namePosition > 0 ? xml.charAt(namePosition - 1) : '\0';
        const char after =
            nameEnd < static_cast<int>(xml.length())
                ? xml.charAt(nameEnd)
                : '\0';

        if (
            (before == '<' || before == ':') &&
            (
                after == '>' ||
                after == '/' ||
                isspace(static_cast<unsigned char>(after))
            )
        ) {
            openingStart = xml.lastIndexOf('<', namePosition);

            if (
                openingStart >= 0 &&
                openingStart + 1 < static_cast<int>(xml.length()) &&
                xml.charAt(openingStart + 1) != '/'
            ) {
                openingEnd = xml.indexOf('>', nameEnd);
                return openingEnd >= 0;
            }
        }

        searchPosition = nameEnd;
    }
}

void invalidateSnapshotTarget()
{
    snapshotTarget = SnapshotTarget{};
}

void resetSnapshotProfileFailover()
{
    snapshotProfileTokenToSkip = String();
    snapshotProfileFailoverPending = false;
}


void applyRuntimeSettings(const RuntimeSettings& previous)
{
    if (
        strcmp(
            previous.snapshotProfileMatch,
            settings.snapshotProfileMatch
        ) != 0
    ) {
        invalidateSnapshotTarget();
        resetSnapshotProfileFailover();
        lastSnapshotResolveAttemptAtMs = 0;
        lastSnapshotResolveStatusCode = 0;
        snapshotTargetFailureStreak = 0;
        consecutiveFailures = 0;
        consecutiveSuccesses = 0;
        consecutiveInconclusiveChecks = 0;
        consecutiveMediaServiceFailures = 0;
        consecutiveIndependentMediaFailures = 0;
        lastSnapshotProbe = SnapshotProbe{};
        lastSnapshotResult = "Perfil alterado; aguardando novo teste JPEG";
    }

    if (previous.cameraRtspPort != settings.cameraRtspPort) {
        camera.rtspPort = settings.cameraRtspPort;
        invalidateLiveProfiles();
        consecutiveIndependentMediaFailures = 0;

        if (camera.valid) {
            saveCameraTarget();
        }
    }

    if (!ENABLE_CAMERA_ALERT_LED) {
        cameraAlertLedBlinking = false;
        writeCameraAlertLed(false);
    }

    if (!ENABLE_EMAIL_ALERTS) {
        pendingIncidentEmail = false;
        pendingRecoveryEmail = false;
    }

    if (
        previous.healthCheckIntervalMs != settings.healthCheckIntervalMs
    ) {
        lastHealthCheckAtMs = millis();
    }

    if (
        previous.onvifRebootWindowMs != settings.onvifRebootWindowMs ||
        previous.onvifRebootCooldownMs != settings.onvifRebootCooldownMs ||
        previous.maxOnvifRebootAttemptsPerWindow !=
            settings.maxOnvifRebootAttemptsPerWindow
    ) {
        refreshCameraRebootWindow();
    }
}

void setState(CameraState newState, const String& event)
{
    if (cameraState != newState) {
        cameraState = newState;
        lastStateChangeAtMs = millis();
    }

    lastEvent = currentTimestamp() + " - " + event;
    Serial.printf("ESTADO: %s | %s\n", stateName(cameraState), event.c_str());
}

bool resendConfigured()
{
    return
        String(RESEND_API_KEY).startsWith("re_") &&
        String(RESEND_API_KEY).indexOf("COLE_SUA") < 0 &&
        String(RESEND_FROM).indexOf("seudominio") < 0 &&
        String(ALERT_EMAIL).indexOf("exemplo.com") < 0;
}

class BoundedStringStream : public Stream {
public:
    BoundedStringStream(String& output, size_t limit)
        : output_(output), limit_(limit)
    {
    }

    size_t write(uint8_t value) override
    {
        return write(&value, 1);
    }

    size_t write(const uint8_t* buffer, size_t size) override
    {
        const size_t outputLength = output_.length();

        if (
            buffer == nullptr ||
            outputLength > limit_ ||
            size > limit_ - outputLength
        ) {
            overflowed_ = true;
            setWriteError();
            return 0;
        }

        if (!output_.concat(
            reinterpret_cast<const char*>(buffer),
            static_cast<unsigned int>(size)
        )) {
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

bool readHttpResponseBodyBounded(
    HTTPClient& request,
    String& output,
    size_t limit,
    bool& limitExceeded,
    bool* streamFailure = nullptr
)
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

    const size_t reserveSize = declaredSize > 0
        ? static_cast<size_t>(declaredSize) + 1
        : min(limit + 1, static_cast<size_t>(1024));

    if (reserveSize > 0 && !output.reserve(reserveSize)) {
        return false;
    }

    BoundedStringStream sink(output, limit);
    const int bytesWritten = request.writeToStream(&sink);
    limitExceeded = sink.overflowed();
    const bool incompleteTransfer =
        bytesWritten < 0 ||
        (declaredSize >= 0 && bytesWritten != declaredSize);

    if (
        streamFailure != nullptr &&
        incompleteTransfer &&
        !limitExceeded &&
        !sink.allocationFailed()
    ) {
        *streamFailure = true;
    }

    return
        bytesWritten >= 0 &&
        !limitExceeded &&
        (declaredSize < 0 || bytesWritten == declaredSize);
}

bool sendResendEmail(const String& subject, const String& html)
{
    lastEmailAttemptAtMs = millis();

    if (!resendConfigured()) {
        lastEmailResult = "Resend nao configurado em include/secrets.h";
        Serial.println(lastEmailResult);
        emailErrors++;
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        lastEmailResult = "Wi-Fi desconectado; envio adiado";
        emailErrors++;
        return false;
    }

    WiFiClientSecure secureClient;

    secureClient.setInsecure();

    HTTPClient request;
    request.setConnectTimeout(10000);
    request.setTimeout(15000);
    request.setReuse(false);

    if (!request.begin(secureClient, RESEND_API_URL)) {
        lastEmailResult = "Falha ao inicializar cliente HTTPS";
        emailErrors++;
        return false;
    }

    request.addHeader("Authorization", String("Bearer ") + RESEND_API_KEY);
    request.addHeader("Content-Type", "application/json");
    request.addHeader(
        "User-Agent",
        String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION
    );

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
    const bool responseRead = statusCode > 0 && readHttpResponseBodyBounded(
        request,
        responseBody,
        MAX_RESEND_RESPONSE_SIZE,
        responseLimitExceeded
    );
    request.end();

    if (responseLimitExceeded) {
        responseBody = "Resposta da API excedeu o limite seguro";
    } else if (statusCode > 0 && !responseRead) {
        responseBody = "Resposta da API indisponivel";
    }

    if (statusCode >= 200 && statusCode < 300) {
        emailsSent++;
        lastEmailResult =
            currentTimestamp() +
            " - enviado; HTTP " +
            String(statusCode) +
            "; " +
            responseBody;

        Serial.println(lastEmailResult);
        return true;
    }

    emailErrors++;
    lastEmailResult =
        currentTimestamp() +
        " - erro; HTTP " +
        String(statusCode) +
        "; " +
        responseBody;

    Serial.println(lastEmailResult);
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
            return "iniciando";
        case CameraState::Discovering:
            return "procurando a câmera";
        case CameraState::Online:
            return "online";
        case CameraState::Degraded:
            return "instável";
        case CameraState::Recovering:
            return "recuperando";
        case CameraState::Offline:
            return "offline";
    }

    return "desconhecido";
}

String describeProbe(const ServiceProbe& probe, const char* protocol)
{
    if (!probe.tcpConnected) {
        return "sem conexão TCP";
    }

    String text;

    if (probe.protocolResponded) {
        text = "respondeu ";
        text += protocol;
        text += ' ';
        text += probe.statusCode;
    } else {
        text = "porta aberta, sem resposta do protocolo";
    }

    text += " · ";
    text += probe.latencyMs;
    text += " ms";
    return text;
}

void appendDetailRow(
    String& html,
    const char* label,
    const String& value,
    uint8_t index
)
{
    html += "<tr style=\"background:";
    html += (index % 2) ? "#f8fafc" : "#ffffff";
    html += "\"><td style=\"padding:11px 18px;font-size:13px;line-height:1.45;"
            "color:#64748b;white-space:nowrap;vertical-align:top;";

    if (index > 0) {
        html += "border-top:1px solid #edf2f7;";
    }

    html += "\">";
    html += htmlEscape(label);
    html += "</td><td style=\"padding:11px 18px;font-size:13px;line-height:1.45;"
            "color:#0f1b2b;font-weight:600;word-break:break-word;vertical-align:top;";

    if (index > 0) {
        html += "border-top:1px solid #edf2f7;";
    }

    html += "\">";
    html += htmlEscape(value);
    html += "</td></tr>";
}

void appendDetailsTable(String& html)
{
    html += "<tr><td style=\"padding:24px 28px 0\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" style=\"border-collapse:separate;"
            "border-spacing:0;border:1px solid #e3e9f0;border-radius:10px;"
            "overflow:hidden\">";

    uint8_t index = 0;

    const auto row = [&](const char* label, const String& value) {
        appendDetailRow(html, label, value, index);
        index++;
    };

    String snapshotDescription = lastSnapshotProbe.detail;

    if (lastSnapshotProbe.jpegValid) {
        snapshotDescription =
            String("JPEG válido · ") + String(lastSnapshotProbe.bytesRead) +
            " bytes · " + String(lastSnapshotProbe.latencyMs) + " ms";
    }

    row("Câmera", CAMERA_NAME);
    row("Estado", stateLabel(cameraState));
    row("Endereço IP", camera.valid ? camera.ip.toString() : "não localizado");
    row(
        "Endpoint ONVIF",
        camera.onvifUrl.isEmpty() ? String("desconhecido") : camera.onvifUrl
    );
    row("Teste de imagem", snapshotDescription);
    row("Teste ONVIF", describeProbe(lastOnvifProbe, "HTTP"));
    row("Teste RTSP", describeProbe(lastRtspProbe, "RTSP"));
    row("Falhas de imagem consecutivas", String(consecutiveFailures));
    row("Falhas de controle consecutivas", String(consecutiveControlFailures));
    row("Recuperação ONVIF", lastCameraRebootResult);
    row(
        "Verificações",
        String(totalHealthChecks) + " no total · " +
            String(failedHealthChecks) + " com falha"
    );
    row(
        "ESP32",
        WiFi.localIP().toString() + " · " + WiFi.SSID() + " · " +
            String(WiFi.RSSI()) + " dBm"
    );
    row("Ligado há", durationText(millis()));
    row("Horário (UTC)", currentTimestamp());

    html += "</table></td></tr>";
}

void appendCallout(String& html, const EmailTheme& theme, const String& text)
{
    html += "<tr><td style=\"padding:22px 28px 0\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\"><tr><td style=\"padding:13px 17px;"
            "background:";
    html += theme.soft;
    html += ";border-left:3px solid ";
    html += theme.accent;
    html += ";border-radius:0 8px 8px 0;font-size:14px;line-height:1.5;color:#334155\">";
    html += text;
    html += "</td></tr></table></td></tr>";
}

String buildEmailHtml(
    const EmailTheme& theme,
    const char* badge,
    const char* title,
    const char* intro,
    const String& callout
)
{
    String html;
    html.reserve(10240);

    html += "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<meta name=\"x-apple-disable-message-reformatting\"></head>"
            "<body style=\"margin:0;padding:0;background:#eef2f7\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" style=\"background:#eef2f7;"
            "padding:26px 12px\"><tr><td align=\"center\">"
            "<table role=\"presentation\" width=\"600\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" style=\"width:100%;max-width:600px;"
            "background:#ffffff;border:1px solid #dfe6ee;border-radius:14px;"
            "overflow:hidden;font-family:-apple-system,BlinkMacSystemFont,"
            "'Segoe UI',Roboto,Helvetica,Arial,sans-serif\">";

    html += "<tr><td style=\"height:4px;line-height:4px;font-size:0;background:";
    html += theme.accent;
    html += "\">&nbsp;</td></tr>";

    html += "<tr><td style=\"padding:26px 28px 0\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\"><tr>"
            "<td style=\"font-size:11px;font-weight:700;letter-spacing:.11em;"
            "text-transform:uppercase;color:#94a3b8\">Camera Watchdog</td>"
            "<td align=\"right\"><span style=\"display:inline-block;"
            "padding:5px 12px;border-radius:999px;font-size:11px;font-weight:700;"
            "letter-spacing:.05em;background:";
    html += theme.soft;
    html += ";color:";
    html += theme.accent;
    html += "\">";
    html += badge;
    html += "</span></td></tr></table></td></tr>";

    html += "<tr><td style=\"padding:16px 28px 0\">"
            "<h1 style=\"margin:0;font-size:23px;line-height:1.25;color:#0f1b2b;"
            "font-weight:700;letter-spacing:-.01em\">";
    html += title;
    html += "</h1><p style=\"margin:9px 0 0;font-size:15px;line-height:1.6;"
            "color:#51617a\">";
    html += intro;
    html += "</p></td></tr>";

    if (!callout.isEmpty()) {
        appendCallout(html, theme, callout);
    }

    appendDetailsTable(html);

    html += "<tr><td style=\"padding:24px 28px 0\">"
            "<a href=\"http://";
    html += WiFi.localIP().toString();
    html += "\" style=\"display:inline-block;background:#0f1b2b;color:#ffffff;"
            "text-decoration:none;padding:12px 22px;border-radius:9px;"
            "font-size:14px;font-weight:600\">Abrir o painel do dispositivo</a>"
            "</td></tr>";

    html += "<tr><td style=\"padding:24px 28px 26px\">"
            "<div style=\"border-top:1px solid #edf2f7;padding-top:16px;"
            "font-size:11.5px;line-height:1.6;color:#94a3b8\">"
            "Mensagem automática do ESP32 Camera Watchdog · firmware ";
    html += FIRMWARE_VERSION;
    html += " · painel em http://";
    html += WiFi.localIP().toString();
    html += "</div></td></tr>";

    html += "</table></td></tr></table></body></html>";
    return html;
}

bool sendIncidentAlert()
{
    if (activeIncident == IncidentKind::None) {
        pendingIncidentEmail = false;
        return true;
    }

    const bool snapshotOnly =
        activeIncident == IncidentKind::SnapshotUnavailable;
    const uint8_t snapshotFailureCount = lastSnapshotProbe.conclusive
        ? consecutiveFailures
        : consecutiveInconclusiveChecks;

    String subject = snapshotOnly
        ? String("[Snapshot indisponivel] ")
        : String("[Camera offline] ");
    subject += CAMERA_NAME;

    String callout;
    callout.reserve(640);

    if (snapshotOnly) {
        callout = "A câmera ainda responde aos serviços de controle, mas falhou "
                  "em <strong>";
        callout += snapshotFailureCount;
        callout += "</strong> verificações consecutivas do snapshot. Último teste: ";
        callout += htmlEscape(lastSnapshotProbe.detail);
        callout += ".";
    } else {
        callout = "O ESP32 não conseguiu obter imagem nem confirmar resposta "
                  "ONVIF/RTSP. Falhas consecutivas: imagem <strong>";
        callout += consecutiveFailures;
        callout += "</strong>, controle <strong>";
        callout += consecutiveControlFailures;
        callout += "</strong>.";
    }

    if (lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Recuperação automática: ";
        callout += htmlEscape(lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(
        EMAIL_THEME_ALERT,
        snapshotOnly ? "SEM SNAPSHOT" : "OFFLINE",
        snapshotOnly
            ? "A câmera parou de entregar snapshots JPEG"
            : "A câmera parou de responder",
        snapshotOnly
            ? "O serviço de controle continua acessível, porém o ESP32 não "
              "recebeu um JPEG completo e válido nas últimas verificações."
            : "A indisponibilidade foi confirmada por testes independentes de "
              "imagem, ONVIF e RTSP.",
        callout
    );

    const bool sent = sendResendEmail(subject, html);

    if (sent) {
        incidentAlertSent = true;
        pendingIncidentEmail = false;
    }

    return sent;
}

bool sendRecoveryAlert()
{
    if (recoveryIncident == IncidentKind::None) {
        pendingRecoveryEmail = false;
        return true;
    }

    const bool recoveredSnapshot =
        recoveryIncident == IncidentKind::SnapshotUnavailable;

    String subject = recoveredSnapshot
        ? String("[Snapshot recuperado] ")
        : String("[Camera recuperada] ");
    subject += CAMERA_NAME;

    String callout = "Duração aproximada do incidente: <strong>";
    callout += durationText(recoveryIncidentDurationMs);
    callout += (
        recoveredSnapshot
            ? "</strong>. O teste atual recebeu um JPEG completo e válido."
            : "</strong>. As verificações atuais confirmaram que a câmera voltou a responder."
    );

    if (lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Última recuperação ONVIF: ";
        callout += htmlEscape(lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(
        EMAIL_THEME_OK,
        "RECUPERADA",
        recoveredSnapshot
            ? "O snapshot JPEG voltou a funcionar"
            : "A câmera voltou a responder",
        "Duas verificações consecutivas confirmaram a recuperação. O "
        "monitoramento normal foi retomado.",
        callout
    );

    const bool sent = sendResendEmail(subject, html);

    if (sent) {
        pendingRecoveryEmail = false;
        recoveryIncident = IncidentKind::None;
        recoveryIncidentDurationMs = 0;
    }

    return sent;
}

bool sendIpChangeAlert(const String& oldIp, const String& newIp)
{
    if (
        !ENABLE_EMAIL_ALERTS ||
        !EMAIL_ON_IP_CHANGE ||
        oldIp.isEmpty() ||
        oldIp == newIp
    ) {
        return true;
    }

    String subject = "[Camera mudou de IP] ";
    subject += CAMERA_NAME;

    String callout = "<strong>";
    callout += htmlEscape(oldIp);
    callout += "</strong> &rarr; <strong>";
    callout += htmlEscape(newIp);
    callout += "</strong>";

    const String html = buildEmailHtml(
        EMAIL_THEME_INFO,
        "NOVO IP",
        "A câmera mudou de endereço",
        "O ESP32 localizou a câmera em um novo IP e já atualizou o endpoint "
        "monitorado. Atualize favoritos e gravadores que apontem para o "
        "endereço antigo.",
        callout
    );

    return sendResendEmail(subject, html);
}

void saveCameraTarget()
{
    if (!camera.valid) {
        return;
    }

    preferences.begin(NVS_NAMESPACE, false);
    preferences.putString("cam_url", camera.onvifUrl);
    preferences.putString("cam_uuid", camera.endpointUuid);
    preferences.putString("cam_scope", camera.scopes);
    preferences.putUShort("rtsp_port", camera.rtspPort);
    preferences.end();
}

bool loadCameraTarget()
{
    preferences.begin(NVS_NAMESPACE, true);
    const String savedUrl = preferences.getString("cam_url", "");
    const String savedUuid = preferences.getString("cam_uuid", "");
    const String savedScopes = preferences.getString("cam_scope", "");
    preferences.end();

    if (savedUrl.isEmpty()) {
        return false;
    }

    ParsedUrl parsed;

    if (!parseHttpUrl(savedUrl, parsed) || parsed.secure) {
        return false;
    }

    camera.valid = true;
    camera.ip = parsed.ip;
    camera.onvifPort = parsed.port;
    camera.rtspPort = CAMERA_RTSP_PORT;
    camera.onvifPath = parsed.path;
    camera.onvifUrl = savedUrl;
    camera.endpointUuid = savedUuid;
    camera.scopes = savedScopes;

    Serial.printf("Camera carregada da NVS: %s\n", camera.onvifUrl.c_str());
    return true;
}

bool loadCameraIpHint()
{
    if (String(CAMERA_IP_HINT).isEmpty()) {
        return false;
    }

    IPAddress hintedIp;

    if (!hintedIp.fromString(CAMERA_IP_HINT)) {
        Serial.println("CAMERA_IP_HINT invalido.");
        return false;
    }

    camera.valid = true;
    camera.ip = hintedIp;
    camera.onvifPort = ONVIF_PORT_CANDIDATES[0];
    camera.rtspPort = CAMERA_RTSP_PORT;
    camera.onvifPath = DEFAULT_ONVIF_PATH;
    camera.onvifUrl = makeOnvifUrl(
        camera.ip,
        camera.onvifPort,
        camera.onvifPath
    );

    Serial.printf("Usando CAMERA_IP_HINT: %s\n", camera.ip.toString().c_str());
    return true;
}

String makeMessageUuid()
{
    char buffer[64];
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    const uint32_t c = esp_random();
    const uint32_t d = esp_random();

    snprintf(
        buffer,
        sizeof(buffer),
        "uuid:%08lx-%04lx-%04lx-%04lx-%04lx%08lx",
        static_cast<unsigned long>(a),
        static_cast<unsigned long>((b >> 16) & 0xFFFF),
        static_cast<unsigned long>(b & 0xFFFF),
        static_cast<unsigned long>((c >> 16) & 0xFFFF),
        static_cast<unsigned long>(c & 0xFFFF),
        static_cast<unsigned long>(d)
    );

    return String(buffer);
}

bool sendDiscoveryProbe(const char* typeName)
{
    String xml;
    xml.reserve(900);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    xml += "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" ";
    xml += "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" ";
    xml += "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" ";
    xml += "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" ";
    xml += "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">";
    xml += "<e:Header>";
    xml += "<w:MessageID>";
    xml += makeMessageUuid();
    xml += "</w:MessageID>";
    xml += "<w:To e:mustUnderstand=\"true\">";
    xml += "urn:schemas-xmlsoap-org:ws:2005:04:discovery";
    xml += "</w:To>";
    xml += "<w:Action e:mustUnderstand=\"true\">";
    xml += "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe";
    xml += "</w:Action>";
    xml += "</e:Header><e:Body><d:Probe>";

    if (typeName != nullptr && strlen(typeName) > 0) {
        xml += "<d:Types>";
        xml += typeName;
        xml += "</d:Types>";
    }

    xml += "</d:Probe></e:Body></e:Envelope>";

    if (!discoveryUdp.beginPacket(WS_DISCOVERY_MULTICAST, WS_DISCOVERY_PORT)) {
        return false;
    }

    const size_t written = discoveryUdp.write(
        reinterpret_cast<const uint8_t*>(xml.c_str()),
        xml.length()
    );

    return written == xml.length() && discoveryUdp.endPacket() == 1;
}

bool candidateAlreadyAdded(const CameraTarget& target)
{
    for (size_t index = 0; index < discoveredCount; index++) {
        if (
            discovered[index].target.ip == target.ip &&
            discovered[index].target.onvifPort == target.onvifPort &&
            discovered[index].target.onvifPath == target.onvifPath
        ) {
            return true;
        }
    }

    return false;
}

void addDiscoveryResponse(const String& xml)
{
    int searchPosition = 0;

    while (discoveredCount < MAX_DISCOVERED_CAMERAS) {
        String xaddrs;
        int nextXaddrs = 0;

        if (!extractLocalTag(
            xml,
            "XAddrs",
            searchPosition,
            xaddrs,
            nextXaddrs
        )) {
            break;
        }

        String endpointUuid;
        String scopes;
        int ignored = 0;
        extractLocalTag(xml, "Address", searchPosition, endpointUuid, ignored);
        extractLocalTag(xml, "Scopes", searchPosition, scopes, ignored);

        const String firstUrl = firstWhitespaceToken(xaddrs);
        ParsedUrl parsed;

        if (
            parseHttpUrl(firstUrl, parsed) &&
            !parsed.secure
        ) {
            CameraTarget target;
            target.valid = true;
            target.ip = parsed.ip;
            target.onvifPort = parsed.port;
            target.rtspPort = CAMERA_RTSP_PORT;
            target.onvifPath = parsed.path;
            target.onvifUrl = firstUrl;
            target.endpointUuid = endpointUuid;
            target.scopes = scopes;

            if (!candidateAlreadyAdded(target)) {
                discovered[discoveredCount].target = target;
                discovered[discoveredCount].rawSummary =
                    endpointUuid + " " + scopes + " " + firstUrl;

                discoveredCount++;
            }
        }

        searchPosition = nextXaddrs;
    }
}

int scoreCandidate(const DiscoveryCandidate& candidate)
{
    int score = 0;
    const String combined = candidate.rawSummary;

    if (
        !camera.endpointUuid.isEmpty() &&
        containsIgnoreCase(combined, camera.endpointUuid)
    ) {
        score += 1000;
    }

    if (
        camera.valid &&
        candidate.target.ip == camera.ip
    ) {
        score += 500;
    }

    if (!String(CAMERA_MATCH).isEmpty()) {
        if (containsIgnoreCase(combined, CAMERA_MATCH)) {
            score += 2000;
        } else {
            return -1;
        }
    }

    if (containsIgnoreCase(candidate.target.scopes, "onvif")) {
        score += 50;
    }

    return score;
}

bool applyBestDiscoveredCandidate()
{
    if (discoveredCount == 0) {
        return false;
    }

    int bestScore = -1;
    int bestIndex = -1;

    for (size_t index = 0; index < discoveredCount; index++) {
        const int score = scoreCandidate(discovered[index]);

        Serial.printf(
            "ONVIF candidato %u: %s | score=%d\n",
            static_cast<unsigned>(index + 1),
            discovered[index].target.onvifUrl.c_str(),
            score
        );

        if (score > bestScore) {
            bestScore = score;
            bestIndex = static_cast<int>(index);
        }
    }

    if (bestIndex < 0) {
        return false;
    }

    const String oldIp = camera.valid ? camera.ip.toString() : "";
    camera = discovered[bestIndex].target;
    invalidateSnapshotTarget();
    invalidateOnvifServiceCatalog();
    resetSnapshotProfileFailover();
    invalidateLiveProfiles();
    lastSnapshotResolveAttemptAtMs = 0;
    consecutiveFailures = 0;
    consecutiveSuccesses = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    consecutiveIndependentMediaFailures = 0;
    snapshotTargetFailureStreak = 0;
    saveCameraTarget();

    const String newIp = camera.ip.toString();

    Serial.printf("Camera selecionada: %s\n", camera.onvifUrl.c_str());

    if (!oldIp.isEmpty() && oldIp != newIp) {
        previousCameraIp = oldIp;
        sendIpChangeAlert(oldIp, newIp);
    }

    return true;
}

bool discoverOnvifCamera()
{
    discoveryRuns++;
    lastRediscoveryAtMs = millis();
    lastEvent = currentTimestamp() + " - Executando ONVIF WS-Discovery";
    Serial.println(lastEvent);
    discoveredCount = 0;

    discoveryUdp.stop();

    if (!discoveryUdp.begin(WS_DISCOVERY_LOCAL_PORT)) {
        lastDiscoveryResult = "Falha ao abrir socket UDP de descoberta";
        return false;
    }

    while (discoveryUdp.parsePacket() > 0) {
        while (discoveryUdp.available()) {
            discoveryUdp.read();
        }
    }

    const bool sentDevice = sendDiscoveryProbe("tds:Device");
    delay(40);
    const bool sentNvt = sendDiscoveryProbe("dn:NetworkVideoTransmitter");
    delay(40);
    const bool sentAny = sendDiscoveryProbe(nullptr);

    if (!sentDevice && !sentNvt && !sentAny) {
        lastDiscoveryResult = "Nao foi possivel enviar o Probe multicast";
        discoveryUdp.stop();
        return false;
    }

    const unsigned long startedAt = millis();

    while (millis() - startedAt < DISCOVERY_TIMEOUT_MS) {
        updateCameraAlertLed();
        httpServer.handleClient();

        const int packetSize = discoveryUdp.parsePacket();

        if (packetSize <= 0) {
            delay(2);
            continue;
        }

        const size_t bytesToRead = min(
            static_cast<size_t>(packetSize),
            WS_DISCOVERY_BUFFER_SIZE
        );

        const int bytesRead = discoveryUdp.read(
            reinterpret_cast<uint8_t*>(discoveryBuffer),
            bytesToRead
        );

        if (bytesRead <= 0) {
            continue;
        }

        discoveryBuffer[bytesRead] = '\0';
        addDiscoveryResponse(String(discoveryBuffer));
    }

    discoveryUdp.stop();

    const bool selected = applyBestDiscoveredCandidate();
    lastDiscoveryResult =
        currentTimestamp() +
        " - candidatos=" +
        String(discoveredCount) +
        "; selecionada=" +
        (selected ? camera.onvifUrl : String("nenhuma"));

    Serial.println(lastDiscoveryResult);
    return selected;
}

ServiceProbe probeOnvif(const CameraTarget& target)
{
    ServiceProbe result;

    if (!target.valid || target.onvifUrl.isEmpty()) {
        return result;
    }

    ParsedUrl parsed;

    if (!parseHttpUrl(target.onvifUrl, parsed) || parsed.secure) {
        return result;
    }

    WiFiClient client;
    const unsigned long startedAt = millis();

    if (!client.connect(parsed.ip, parsed.port, TCP_CONNECT_TIMEOUT_MS)) {
        result.latencyMs = millis() - startedAt;
        return result;
    }

    result.tcpConnected = true;
    result.latencyMs = millis() - startedAt;

    String hostHeader = parsed.ip.toString();

    if (parsed.port != 80) {
        hostHeader += ':';
        hostHeader += parsed.port;
    }

    client.print("POST ");
    client.print(parsed.path);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(hostHeader);
    client.print("\r\nContent-Type: application/soap+xml; charset=utf-8; action=\"");
    client.print("http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime");
    client.print("\"\r\nConnection: close\r\nContent-Length: ");
    client.print(strlen(SOAP_GET_SYSTEM_TIME));
    client.print("\r\n\r\n");
    client.print(SOAP_GET_SYSTEM_TIME);

    const unsigned long responseStartedAt = millis();
    String responseHeader;
    responseHeader.reserve(512);

    while (millis() - responseStartedAt < SERVICE_RESPONSE_TIMEOUT_MS) {
        updateCameraAlertLed();

        while (client.available()) {
            const char value = static_cast<char>(client.read());

            if (responseHeader.length() < 1024) {
                responseHeader += value;
            }
        }

        if (responseHeader.indexOf("\r\n\r\n") >= 0) {
            break;
        }

        if (!client.connected() && !client.available()) {
            break;
        }

        delay(1);
    }

    client.stop();
    result.latencyMs = millis() - startedAt;

    const int firstLineEnd = responseHeader.indexOf("\r\n");
    const String firstLine = firstLineEnd >= 0
        ? responseHeader.substring(0, firstLineEnd)
        : responseHeader;

    if (firstLine.startsWith("HTTP/")) {
        const int firstSpace = firstLine.indexOf(' ');

        if (firstSpace >= 0) {
            result.statusCode = firstLine.substring(firstSpace + 1).toInt();
        }
    }

    String normalizedHeader = responseHeader;
    normalizedHeader.toLowerCase();

    result.protocolResponded =
        result.statusCode == 401 ||
        result.statusCode == 403 ||
        normalizedHeader.indexOf("content-type: application/soap+xml") >= 0 ||
        normalizedHeader.indexOf("content-type: text/xml") >= 0;

    return result;
}

bool resolveCameraOnvifEndpoint()
{
    if (!camera.valid) {
        return false;
    }

    ServiceProbe currentProbe = probeOnvif(camera);

    if (currentProbe.protocolResponded) {
        lastOnvifProbe = currentProbe;
        return true;
    }

    for (size_t portIndex = 0;
         portIndex < ONVIF_PORT_CANDIDATE_COUNT;
         portIndex++) {
        const uint16_t candidatePort = ONVIF_PORT_CANDIDATES[portIndex];

        if (
            candidatePort == camera.onvifPort &&
            camera.onvifPath == DEFAULT_ONVIF_PATH
        ) {
            continue;
        }

        CameraTarget candidate = camera;
        candidate.onvifPort = candidatePort;
        candidate.onvifPath = DEFAULT_ONVIF_PATH;
        candidate.onvifUrl = makeOnvifUrl(
            candidate.ip,
            candidate.onvifPort,
            candidate.onvifPath
        );

        const ServiceProbe candidateProbe = probeOnvif(candidate);

        if (!candidateProbe.protocolResponded) {
            continue;
        }

        camera.onvifPort = candidate.onvifPort;
        camera.onvifPath = candidate.onvifPath;
        camera.onvifUrl = candidate.onvifUrl;
        lastOnvifProbe = candidateProbe;
        invalidateSnapshotTarget();
        invalidateOnvifServiceCatalog();
        resetSnapshotProfileFailover();
        invalidateLiveProfiles();
        lastSnapshotResolveAttemptAtMs = 0;
        saveCameraTarget();

        Serial.printf(
            "Servico ONVIF confirmado na porta %u.\n",
            camera.onvifPort
        );
        return true;
    }

    lastOnvifProbe = currentProbe;
    return false;
}

bool postOnvifSoap(
    const String& serviceUrl,
    const char* action,
    const String& body,
    String& responseBody,
    bool requireResponseBody = true,
    int* finalStatusCode = nullptr,
    bool* transportFailure = nullptr,
    uint32_t timeoutMs = SERVICE_RESPONSE_TIMEOUT_MS
)
{
    responseBody = String();

    if (transportFailure != nullptr) {
        *transportFailure = false;
    }

    const String wsseHeader = makeWsseSecurityHeader();
    String envelope;
    envelope.reserve(body.length() + wsseHeader.length() + 420);
    envelope += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    envelope += "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" ";
    envelope += "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" ";
    envelope += "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" ";
    envelope += "xmlns:tr2=\"http://www.onvif.org/ver20/media/wsdl\" ";
    envelope += "xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" ";
    envelope += "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" ";
    envelope += "xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\" ";
    envelope += "xmlns:tan=\"http://www.onvif.org/ver20/analytics/wsdl\" ";
    envelope += "xmlns:tmd=\"http://www.onvif.org/ver10/deviceIO/wsdl\" ";
    envelope += "xmlns:tt=\"http://www.onvif.org/ver10/schema\">";
    envelope += wsseHeader;
    envelope += "<s:Body>";
    envelope += body;
    envelope += "</s:Body></s:Envelope>";

    WiFiClient client;
    HTTPClient request;
    const char* responseHeaders[] = {"WWW-Authenticate"};
    request.setConnectTimeout(min(TCP_CONNECT_TIMEOUT_MS, timeoutMs));
    request.setTimeout(timeoutMs);
    request.setReuse(false);
    request.useHTTP10(true);
    request.setUserAgent(
        String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION
    );
    request.collectHeaders(responseHeaders, 1);

    String contentType =
        "application/soap+xml; charset=utf-8; action=\"";
    contentType += action;
    contentType += '\"';

    const auto executeAttempt = [&](const String& digestAuthorization) {
        if (!request.begin(client, serviceUrl)) {
            return -1;
        }

        request.addHeader("Content-Type", contentType);
        applyCameraAuthorization(request, digestAuthorization);
        return request.POST(envelope);
    };

    int statusCode = executeAttempt(String());

    if (statusCode == 401 && cameraCredentialsConfigured()) {
        DigestChallenge challenge;
        const String authenticateHeader = request.header("WWW-Authenticate");

        if (parseDigestChallenge(authenticateHeader, challenge)) {
            const String digestAuthorization = makeDigestAuthorization(
                "POST",
                serviceUrl,
                challenge
            );

            if (!digestAuthorization.isEmpty()) {
                request.end();
                client.stop();
                statusCode = executeAttempt(digestAuthorization);
            }
        }
    }

    if (finalStatusCode != nullptr) {
        *finalStatusCode = statusCode;
    }

    const int responseSize = request.getSize();
    bool responseLimitExceeded =
        responseSize > static_cast<int>(MAX_ONVIF_RESPONSE_SIZE);
    bool responseRead = false;
    bool responseStreamFailure = false;

    if (statusCode > 0 && !responseLimitExceeded) {
        responseRead = readHttpResponseBodyBounded(
            request,
            responseBody,
            MAX_ONVIF_RESPONSE_SIZE,
            responseLimitExceeded,
            &responseStreamFailure
        );
    }

    if (transportFailure != nullptr) {
        *transportFailure = statusCode < 0 || responseStreamFailure;
    }

    request.end();

    int faultStart = 0;
    int faultEnd = 0;
    const bool soapFault =
        !responseBody.isEmpty() &&
        findNextOpeningTag(
            responseBody,
            "Fault",
            0,
            faultStart,
            faultEnd
        );

    if (
        statusCode < 200 ||
        statusCode >= 300 ||
        responseLimitExceeded ||
        soapFault
    ) {
        return false;
    }

    return
        !requireResponseBody ||
        (responseRead && !responseBody.isEmpty());
}

void invalidateOnvifServiceCatalog()
{
    onvifServices = OnvifServiceCatalog{};
    onvifMetadata = OnvifDeviceMetadata{};
    onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
    onvifMetadataStageFailureStreak = 0;
    lastOnvifMetadataRequestAtMs = 0;
    lastOnvifMetadataRequestFailed = false;
    lastBrowserSnapshotDeliveredAtMs = 0;
    lastBrowserSnapshotBytes = 0;
    lastBrowserSnapshotLatencyMs = 0;
    lastMediaCanaryProbe = LiveMediaProbeResult{};
    lastMediaCanaryAtMs = 0;
    lastMediaCanaryProfileIndex = 0;
    lastMediaCanaryProfilesTested = 0;
    lastHealthEvidenceSource = HealthEvidenceSource::None;
    lastHealthEvidenceAtMs = 0;
}

String limitedOnvifText(String value, size_t maximumLength = 120)
{
    value = xmlUnescape(value);
    value.trim();

    if (value.length() > maximumLength) {
        value.remove(maximumLength);
    }

    return value;
}

bool normalizeAdvertisedOnvifUrl(
    const String& advertisedUrl,
    String& normalizedUrl
)
{
    ParsedHttpResource parsed;

    if (
        !camera.valid ||
        !parseHttpResourceUrl(advertisedUrl, parsed) ||
        parsed.secure
    ) {
        return false;
    }

    IPAddress advertisedIp;

    if (!advertisedIp.fromString(parsed.host.c_str())) {
        return false;
    }

    const bool unspecifiedAddress =
        advertisedIp[0] == 0 &&
        advertisedIp[1] == 0 &&
        advertisedIp[2] == 0 &&
        advertisedIp[3] == 0;

    if (!unspecifiedAddress && advertisedIp != camera.ip) {
        return false;
    }

    normalizedUrl = makeOnvifUrl(camera.ip, parsed.port, parsed.path);
    return true;
}

OnvifServiceEndpoint* endpointForOnvifNamespace(const String& serviceNamespace)
{
    if (serviceNamespace == "http://www.onvif.org/ver10/device/wsdl") {
        return &onvifServices.device;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/media/wsdl") {
        return &onvifServices.media1;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/media/wsdl") {
        return &onvifServices.media2;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/events/wsdl") {
        return &onvifServices.events;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/ptz/wsdl") {
        return &onvifServices.ptz;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/imaging/wsdl") {
        return &onvifServices.imaging;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/analytics/wsdl") {
        return &onvifServices.analytics;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/deviceIO/wsdl") {
        return &onvifServices.deviceIo;
    }

    return nullptr;
}

bool onvifCapabilityIsTrue(const String& serviceXml, const char* name)
{
    const String doubleQuoted = String(name) + "=\"true\"";
    const String singleQuoted = String(name) + "='true'";
    const String doubleQuotedOne = String(name) + "=\"1\"";
    const String singleQuotedOne = String(name) + "='1'";

    if (
        containsIgnoreCase(serviceXml, doubleQuoted) ||
        containsIgnoreCase(serviceXml, singleQuoted) ||
        serviceXml.indexOf(doubleQuotedOne) >= 0 ||
        serviceXml.indexOf(singleQuotedOne) >= 0
    ) {
        return true;
    }

    String value;
    int ignored = 0;

    if (!extractLocalTag(serviceXml, name, 0, value, ignored)) {
        return false;
    }

    value.trim();
    return value == "1" || containsIgnoreCase(value, "true");
}

uint16_t onvifUnsignedTag(const String& xml, const char* name)
{
    String value;
    int ignored = 0;

    if (!extractLocalTag(xml, name, 0, value, ignored)) {
        return 0;
    }

    const long parsed = value.toInt();
    return parsed > 0 && parsed <= 65535
        ? static_cast<uint16_t>(parsed)
        : 0;
}

void collectOnvifServiceCapabilities(
    const String& serviceNamespace,
    const String& serviceXml
)
{
    if (serviceNamespace == "http://www.onvif.org/ver10/device/wsdl") {
        onvifServices.usernameToken = onvifCapabilityIsTrue(
            serviceXml,
            "UsernameToken"
        );
        onvifServices.httpDigest = onvifCapabilityIsTrue(
            serviceXml,
            "HttpDigest"
        );
    } else if (
        serviceNamespace == "http://www.onvif.org/ver10/media/wsdl"
    ) {
        onvifServices.mediaSnapshotUri = onvifCapabilityIsTrue(
            serviceXml,
            "SnapshotUri"
        );
        onvifServices.mediaRtpRtspTcp = onvifCapabilityIsTrue(
            serviceXml,
            "RTP_RTSP_TCP"
        );
    } else if (
        serviceNamespace == "http://www.onvif.org/ver20/media/wsdl"
    ) {
        onvifServices.media2RtspStreaming = onvifCapabilityIsTrue(
            serviceXml,
            "RTSPStreaming"
        );
    } else if (
        serviceNamespace == "http://www.onvif.org/ver10/events/wsdl"
    ) {
        onvifServices.eventsPullPoint = onvifCapabilityIsTrue(
            serviceXml,
            "WSPullPointSupport"
        );
    } else if (
        serviceNamespace == "http://www.onvif.org/ver20/ptz/wsdl"
    ) {
        onvifServices.ptzStatus = onvifCapabilityIsTrue(
            serviceXml,
            "MoveStatus"
        ) || onvifCapabilityIsTrue(serviceXml, "StatusPosition");
    } else if (
        serviceNamespace == "http://www.onvif.org/ver20/imaging/wsdl"
    ) {
        onvifServices.imagingStabilizationAdvertised = onvifCapabilityIsTrue(
            serviceXml,
            "ImageStabilization"
        );
    } else if (
        serviceNamespace == "http://www.onvif.org/ver20/analytics/wsdl"
    ) {
        onvifServices.analyticsRules = onvifCapabilityIsTrue(
            serviceXml,
            "RuleSupport"
        );
        onvifServices.analyticsModules = onvifCapabilityIsTrue(
            serviceXml,
            "AnalyticsModuleSupport"
        );
    }
}

bool parseOnvifServices(const String& servicesXml)
{
    int serviceStart = 0;
    int serviceOpenEnd = 0;
    int searchPosition = 0;

    while (findNextOpeningTag(
        servicesXml,
        "Service",
        searchPosition,
        serviceStart,
        serviceOpenEnd
    )) {
        int nextServiceStart = 0;
        int nextServiceOpenEnd = 0;
        const bool hasNextService = findNextOpeningTag(
            servicesXml,
            "Service",
            serviceOpenEnd + 1,
            nextServiceStart,
            nextServiceOpenEnd
        );
        const int serviceEnd = hasNextService
            ? nextServiceStart
            : servicesXml.length();
        const String serviceXml = servicesXml.substring(
            serviceStart,
            serviceEnd
        );
        String serviceNamespace;
        String advertisedUrl;
        int ignored = 0;

        if (
            extractLocalTag(
                serviceXml,
                "Namespace",
                0,
                serviceNamespace,
                ignored
            ) &&
            extractLocalTag(
                serviceXml,
                "XAddr",
                0,
                advertisedUrl,
                ignored
            )
        ) {
            serviceNamespace = limitedOnvifText(serviceNamespace, 96);
            advertisedUrl = limitedOnvifText(advertisedUrl, 512);
            OnvifServiceEndpoint* endpoint = endpointForOnvifNamespace(
                serviceNamespace
            );

            if (endpoint != nullptr && !endpoint->available) {
                String normalizedUrl;

                if (normalizeAdvertisedOnvifUrl(
                    advertisedUrl,
                    normalizedUrl
                )) {
                    endpoint->available = true;
                    endpoint->url = normalizedUrl;
                    endpoint->majorVersion = onvifUnsignedTag(
                        serviceXml,
                        "Major"
                    );
                    endpoint->minorVersion = onvifUnsignedTag(
                        serviceXml,
                        "Minor"
                    );
                    onvifServices.serviceCount++;
                    collectOnvifServiceCapabilities(
                        serviceNamespace,
                        serviceXml
                    );
                } else {
                    onvifServices.rejectedEndpoints++;
                }
            }
        }

        if (!hasNextService) {
            break;
        }

        searchPosition = nextServiceStart;
    }

    return onvifServices.serviceCount > 0;
}

bool resolveOnvifServiceCatalog(bool forceRefresh)
{
    if (onvifServices.resolved && !forceRefresh) {
        const bool fallbackRetryDue =
            onvifServices.usedMediaFallback &&
            millis() - onvifServices.resolvedAtMs >=
                ONVIF_CATALOG_FALLBACK_RETRY_MS;

        if (!fallbackRetryDue) {
            return onvifServices.media1.available;
        }

    }

    const uint32_t previousAttempts = onvifServices.attempts;
    const uint32_t previousSuccesses = onvifServices.successes;
    const uint32_t previousFailures = onvifServices.failures;
    const bool previousUsedMediaFallback =
        onvifServices.usedMediaFallback;
    onvifServices = OnvifServiceCatalog{};
    onvifServices.attempts = previousAttempts + 1;
    onvifServices.successes = previousSuccesses;
    onvifServices.failures = previousFailures;
    const unsigned long startedAtMs = millis();
    String responseXml;
    int statusCode = 0;
    bool transportFailure = false;

    const bool received = postOnvifSoap(
        camera.onvifUrl,
        "http://www.onvif.org/ver10/device/wsdl/GetServices",
        "<tds:GetServices><tds:IncludeCapability>true</tds:IncludeCapability></tds:GetServices>",
        responseXml,
        true,
        &statusCode,
        &transportFailure
    );

    onvifServices.lastLatencyMs = millis() - startedAtMs;
    onvifServices.lastStatusCode = statusCode;

    if (received && parseOnvifServices(responseXml)) {
        onvifServices.successes++;
        onvifServices.lastResult =
            String("GetServices retornou ") +
            String(onvifServices.serviceCount) +
            " servicos validos";
    } else {
        onvifServices.failures++;
        onvifServices.lastResult = transportFailure
            ? "GetServices falhou no transporte; Media1 convencional preservado"
            : String("GetServices indisponivel; HTTP ") +
                String(statusCode) +
                "; Media1 convencional preservado";
    }

    if (!onvifServices.device.available) {
        onvifServices.device.available = true;
        onvifServices.device.url = camera.onvifUrl;
        onvifServices.device.majorVersion = 1;
    }

    if (!onvifServices.media1.available) {
        onvifServices.media1.available = true;
        onvifServices.media1.url = makeOnvifUrl(
            camera.ip,
            camera.onvifPort,
            DEFAULT_ONVIF_MEDIA_PATH
        );
        onvifServices.media1.majorVersion = 1;
        onvifServices.usedMediaFallback = true;
    }

    onvifServices.resolved = true;
    onvifServices.resolvedAtMs = millis();

    if (previousUsedMediaFallback && !onvifServices.usedMediaFallback) {
        onvifMetadata = OnvifDeviceMetadata{};
        onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
        onvifMetadataStageFailureStreak = 0;
        lastOnvifMetadataRequestAtMs = 0;
        lastOnvifMetadataRequestFailed = false;
    }

    return onvifServices.media1.available;
}

uint16_t countOnvifOpeningTags(const String& xml, const char* localName)
{
    uint16_t count = 0;
    int start = 0;
    int end = 0;
    int position = 0;

    while (findNextOpeningTag(xml, localName, position, start, end)) {
        if (count < UINT16_MAX) {
            count++;
        }

        position = end + 1;
    }

    return count;
}

uint16_t countOnvifTopicMarkers(const String& xml)
{
    uint16_t count = 0;
    const char* markers[] = {
        "topic=\"true\"",
        "topic='true'"
    };

    for (const char* marker : markers) {
        int position = 0;
        const size_t markerLength = strlen(marker);

        while ((position = xml.indexOf(marker, position)) >= 0) {
            if (count < UINT16_MAX) {
                count++;
            }

            position += markerLength;
        }
    }

    return count;
}

void completeOnvifMetadataRefresh()
{
    onvifMetadataStage = OnvifMetadataStage::Complete;
    onvifMetadata.loadedAtMs = millis();
    onvifMetadataStageFailureStreak = 0;
    lastOnvifMetadataRequestFailed = false;
    onvifMetadata.lastResult =
        String(onvifMetadata.requests - onvifMetadata.failures) +
        "/" + String(onvifMetadata.requests) +
        " consultas ONVIF de leitura concluidas";
}

void advanceOnvifMetadataStage()
{
    switch (onvifMetadataStage) {
        case OnvifMetadataStage::DeviceInformation:
            onvifMetadataStage = OnvifMetadataStage::Scopes;
            break;
        case OnvifMetadataStage::Scopes:
            onvifMetadataStage = OnvifMetadataStage::SystemTime;
            break;
        case OnvifMetadataStage::SystemTime:
            onvifMetadataStage = OnvifMetadataStage::EventProperties;
            break;
        case OnvifMetadataStage::EventProperties:
            completeOnvifMetadataRefresh();
            break;
        case OnvifMetadataStage::Complete:
            break;
    }
}

void noteOnvifMetadataFailure(const char* operation, int statusCode)
{
    onvifMetadata.failures++;

    if (onvifMetadataStageFailureStreak < 255) {
        onvifMetadataStageFailureStreak++;
    }

    const bool permanentStatus =
        (
            statusCode >= 400 &&
            statusCode <= 499 &&
            statusCode != 408 &&
            statusCode != 429
        ) || statusCode == 501;
    const bool retryLimitReached =
        onvifMetadataStageFailureStreak >= 3;

    if (permanentStatus || retryLimitReached) {
        onvifMetadata.lastResult = String(operation) +
            " nao suportado ou indisponivel; etapa ignorada";
        onvifMetadataStageFailureStreak = 0;
        lastOnvifMetadataRequestFailed = false;
        advanceOnvifMetadataStage();
        return;
    }

    lastOnvifMetadataRequestFailed = true;
    onvifMetadata.lastResult = String(operation) + " falhou; HTTP " +
        String(statusCode) + "; nova tentativa em 30 s";
}

void refreshOnvifDeviceMetadata()
{
    if (
        !camera.valid ||
        WiFi.status() != WL_CONNECTED
    ) {
        return;
    }

    const unsigned long now = millis();

    if (onvifMetadataStage == OnvifMetadataStage::Complete) {
        const bool fallbackRetryDue =
            onvifServices.usedMediaFallback &&
            now - onvifServices.resolvedAtMs >=
                ONVIF_CATALOG_FALLBACK_RETRY_MS;

        if (!fallbackRetryDue) {
            return;
        }

        CameraIoGuard cameraIoGuard;

        if (cameraIoGuard.acquired()) {
            resolveOnvifServiceCatalog();
        }

        return;
    }

    const unsigned long retryIntervalMs = lastOnvifMetadataRequestFailed
        ? 30000UL
        : 250UL;

    if (
        lastOnvifMetadataRequestAtMs != 0 &&
        now - lastOnvifMetadataRequestAtMs < retryIntervalMs
    ) {
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        return;
    }

    if (!resolveOnvifServiceCatalog()) {
        lastOnvifMetadataRequestAtMs = now;
        lastOnvifMetadataRequestFailed = true;
        onvifMetadata.lastResult =
            "Catalogo ONVIF indisponivel; nova tentativa em 30 s";
        return;
    }

    String responseXml;
    int statusCode = 0;
    bool succeeded = false;
    onvifMetadata.requests++;
    lastOnvifMetadataRequestAtMs = millis();

    switch (onvifMetadataStage) {
        case OnvifMetadataStage::DeviceInformation: {
            succeeded = postOnvifSoap(
                onvifServices.device.url,
                "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation",
                "<tds:GetDeviceInformation/>",
                responseXml,
                true,
                &statusCode,
                nullptr,
                1000UL
            );

            if (!succeeded) {
                noteOnvifMetadataFailure("GetDeviceInformation", statusCode);
                return;
            }

            String value;
            int ignored = 0;

            if (extractLocalTag(responseXml, "Manufacturer", 0, value, ignored)) {
                onvifMetadata.manufacturer = limitedOnvifText(value);
            }

            if (extractLocalTag(responseXml, "Model", 0, value, ignored)) {
                onvifMetadata.model = limitedOnvifText(value);
            }

            if (extractLocalTag(
                responseXml,
                "FirmwareVersion",
                0,
                value,
                ignored
            )) {
                onvifMetadata.firmware = limitedOnvifText(value);
            }

            onvifMetadata.loaded = true;
            onvifMetadataStage = OnvifMetadataStage::Scopes;
            break;
        }

        case OnvifMetadataStage::Scopes:
            succeeded = postOnvifSoap(
                onvifServices.device.url,
                "http://www.onvif.org/ver10/device/wsdl/GetScopes",
                "<tds:GetScopes/>",
                responseXml,
                true,
                &statusCode,
                nullptr,
                1000UL
            );

            if (!succeeded) {
                noteOnvifMetadataFailure("GetScopes", statusCode);
                return;
            }

            onvifMetadata.scopeCount = countOnvifOpeningTags(
                responseXml,
                "Scopes"
            );
            onvifMetadata.profileStreaming =
                responseXml.indexOf("Profile/Streaming") >= 0 ||
                responseXml.indexOf("Profile%2FStreaming") >= 0;
            onvifMetadata.profileT =
                responseXml.indexOf("Profile/T") >= 0 ||
                responseXml.indexOf("Profile%2FT") >= 0;
            onvifMetadata.scopesLoaded = true;
            onvifMetadataStage = OnvifMetadataStage::SystemTime;
            break;

        case OnvifMetadataStage::SystemTime: {
            succeeded = postOnvifSoap(
                onvifServices.device.url,
                "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime",
                "<tds:GetSystemDateAndTime/>",
                responseXml,
                true,
                &statusCode,
                nullptr,
                1000UL
            );

            if (!succeeded) {
                noteOnvifMetadataFailure("GetSystemDateAndTime", statusCode);
                return;
            }

            String value;
            int ignored = 0;

            if (extractLocalTag(responseXml, "DateTimeType", 0, value, ignored)) {
                onvifMetadata.dateTimeType = limitedOnvifText(value, 32);
            }

            if (extractLocalTag(responseXml, "TZ", 0, value, ignored)) {
                onvifMetadata.timeZone = limitedOnvifText(value, 48);
            }

            onvifMetadata.timeLoaded = true;
            onvifMetadataStage = OnvifMetadataStage::EventProperties;
            break;
        }

        case OnvifMetadataStage::EventProperties:
            if (!onvifServices.events.available) {
                onvifMetadata.requests--;
                completeOnvifMetadataRefresh();
                return;
            }

            succeeded = postOnvifSoap(
                onvifServices.events.url,
                "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesRequest",
                "<tev:GetEventProperties/>",
                responseXml,
                true,
                &statusCode,
                nullptr,
                1000UL
            );

            if (!succeeded) {
                noteOnvifMetadataFailure("GetEventProperties", statusCode);
                return;
            }

            onvifMetadata.eventTopicCount = countOnvifTopicMarkers(
                responseXml
            );
            onvifMetadata.eventDescriptionCount = countOnvifOpeningTags(
                responseXml,
                "MessageDescription"
            );
            onvifMetadata.motionEvents =
                responseXml.indexOf("Motion") >= 0;
            onvifMetadata.tamperEvents =
                responseXml.indexOf("Tamper") >= 0;
            onvifMetadata.signalLossEvents =
                responseXml.indexOf("SignalLoss") >= 0;
            onvifMetadata.imageQualityEvents =
                responseXml.indexOf("ImageTooBlurry") >= 0 ||
                responseXml.indexOf("ImageTooDark") >= 0 ||
                responseXml.indexOf("ImageTooBright") >= 0;
            onvifMetadata.eventPropertiesLoaded = true;
            completeOnvifMetadataRefresh();
            return;

        case OnvifMetadataStage::Complete:
            return;
    }

    onvifMetadataStageFailureStreak = 0;
    lastOnvifMetadataRequestFailed = false;
    onvifMetadata.lastResult =
        "Inventario ONVIF em andamento";
}

bool requestLiveSynchronizationPoint(
    const char* profileToken,
    String& result
)
{
    result = String();

    if (profileToken == nullptr || profileToken[0] == '\0') {
        result = "Perfil sem token para SetSynchronizationPoint";
        return false;
    }

    if (!onvifServices.resolved || !onvifServices.media1.available) {
        result = "Servico Media1 indisponivel para SetSynchronizationPoint";
        return false;
    }

    const String token(profileToken);

    if (token.length() > 128) {
        result = "Token do perfil excedeu o limite de seguranca";
        return false;
    }

    String requestBody;
    requestBody.reserve(token.length() + 120);
    requestBody += "<trt:SetSynchronizationPoint><trt:ProfileToken>";
    requestBody += htmlEscape(token);
    requestBody += "</trt:ProfileToken></trt:SetSynchronizationPoint>";

    String responseXml;
    int statusCode = 0;
    const bool accepted = postOnvifSoap(
        onvifServices.media1.url,
        "http://www.onvif.org/ver10/media/wsdl/SetSynchronizationPoint",
        requestBody,
        responseXml,
        false,
        &statusCode,
        nullptr,
        500UL
    );

    result = accepted
        ? String("SetSynchronizationPoint aceito; HTTP ") + String(statusCode)
        : String("SetSynchronizationPoint falhou; HTTP ") + String(statusCode);
    return accepted;
}

#include "live_catalog.h"

void clearMediaCanaryFailureEvidence()
{
    mediaCanarySilentProfilesMask = 0;

    for (size_t index = 0; index < MAX_LIVE_PROFILES; index++) {
        mediaCanarySilentProfileAtMs[index] = 0;
    }
}

void resetMediaCanaryFailureEvidence()
{
    clearMediaCanaryFailureEvidence();
    mediaCanaryNextProfileOffset = 0;
}

bool selectSnapshotProfile(
    const String& profilesXml,
    String& selectedToken,
    String& selectedName
)
{
    LiveProfile profiles[MAX_LIVE_PROFILES];
    const size_t profileCount = collectLiveProfiles(profilesXml, profiles);
    int matchingJpegIndex = -1;
    int jpegIndex = -1;
    int matchingIndex = -1;
    int fallbackIndex = -1;
    int skippedIndex = -1;
    uint32_t fallbackPixels = UINT32_MAX;

    for (size_t index = 0; index < profileCount; index++) {
        const LiveProfile& profile = profiles[index];

        if (!profile.valid || profile.token.isEmpty()) {
            continue;
        }

        if (
            !snapshotProfileTokenToSkip.isEmpty() &&
            profile.token == snapshotProfileTokenToSkip
        ) {
            skippedIndex = static_cast<int>(index);
            continue;
        }

        const uint32_t pixels =
            profile.width > 0 && profile.height > 0
                ? static_cast<uint32_t>(profile.width) * profile.height
                : UINT32_MAX;

        if (fallbackIndex < 0 || pixels < fallbackPixels) {
            fallbackIndex = static_cast<int>(index);
            fallbackPixels = pixels;
        }

        const String encoding = normalizedLiveEncoding(profile.encoding);
        const bool jpegProfile = encoding == "JPEG" || encoding == "MJPEG";
        const bool nameMatches =
            !String(CAMERA_SNAPSHOT_PROFILE_MATCH).isEmpty() &&
            containsIgnoreCase(
                profile.name,
                CAMERA_SNAPSHOT_PROFILE_MATCH
            );

        if (nameMatches && jpegProfile && matchingJpegIndex < 0) {
            matchingJpegIndex = static_cast<int>(index);
        }

        if (jpegProfile && jpegIndex < 0) {
            jpegIndex = static_cast<int>(index);
        }

        if (nameMatches && matchingIndex < 0) {
            matchingIndex = static_cast<int>(index);
        }
    }

    const int selectedIndex = matchingJpegIndex >= 0
        ? matchingJpegIndex
        : matchingIndex >= 0
            ? matchingIndex
            : jpegIndex >= 0
                ? jpegIndex
                : fallbackIndex >= 0
                    ? fallbackIndex
                    : skippedIndex;

    if (selectedIndex < 0) {
        return false;
    }

    selectedToken = profiles[selectedIndex].token;
    selectedName = profiles[selectedIndex].name;

    if (selectedToken == snapshotProfileTokenToSkip) {
        resetSnapshotProfileFailover();
    }

    return true;
}

bool resolveSnapshotTarget()
{
    lastSnapshotResolveAttemptAtMs = millis();
    lastSnapshotResolveStatusCode = 0;
    lastSnapshotResolveTransportFailure = false;
    invalidateSnapshotTarget();

    if (!camera.valid || WiFi.status() != WL_CONNECTED) {
        lastSnapshotResult = "Camera ou Wi-Fi indisponivel";
        return false;
    }

    if (
        !lastOnvifProbe.protocolResponded &&
        !resolveCameraOnvifEndpoint()
    ) {
        lastSnapshotResolveStatusCode = lastOnvifProbe.statusCode;
        lastSnapshotResult = "Servico ONVIF nao localizado";
        return false;
    }

    lastSnapshotResolveAttemptAtMs = millis();

    if (!resolveOnvifServiceCatalog()) {
        lastSnapshotResult = "Catalogo ONVIF sem servico Media1";
        return false;
    }

    const String mediaUrl = onvifServices.media1.url;

    String profileToken;
    String profileName;

    {
        String profilesXml;
        int profilesStatusCode = 0;
        bool profilesTransportFailure = false;

        if (!postOnvifSoap(
            mediaUrl,
            "http://www.onvif.org/ver10/media/wsdl/GetProfiles",
            "<trt:GetProfiles/>",
            profilesXml,
            true,
            &profilesStatusCode,
            &profilesTransportFailure
        )) {
            lastSnapshotResolveStatusCode = profilesStatusCode;
            lastSnapshotResolveTransportFailure = profilesTransportFailure;
            lastSnapshotResult = "ONVIF nao retornou os perfis de midia";
            return false;
        }

        lastSnapshotResolveStatusCode = profilesStatusCode;
        publishLiveProfilesFromXml(profilesXml, mediaUrl, camera.ip);

        if (!selectSnapshotProfile(
            profilesXml,
            profileToken,
            profileName
        )) {
            lastSnapshotResult = "Nenhum perfil ONVIF compativel";
            return false;
        }
    }

    String snapshotRequest;
    snapshotRequest.reserve(profileToken.length() + 96);
    snapshotRequest += "<trt:GetSnapshotUri><trt:ProfileToken>";
    snapshotRequest += htmlEscape(profileToken);
    snapshotRequest += "</trt:ProfileToken></trt:GetSnapshotUri>";

    String snapshotXml;
    int snapshotStatusCode = 0;
    bool snapshotTransportFailure = false;

    if (!postOnvifSoap(
        mediaUrl,
        "http://www.onvif.org/ver10/media/wsdl/GetSnapshotUri",
        snapshotRequest,
        snapshotXml,
        true,
        &snapshotStatusCode,
        &snapshotTransportFailure
    )) {
        lastSnapshotResolveStatusCode = snapshotStatusCode;
        lastSnapshotResolveTransportFailure = snapshotTransportFailure;
        lastSnapshotResult = "ONVIF nao retornou o snapshot";
        return false;
    }

    lastSnapshotResolveStatusCode = snapshotStatusCode;

    String snapshotUrl;
    int ignoredPosition = 0;

    if (!extractLocalTag(
        snapshotXml,
        "Uri",
        0,
        snapshotUrl,
        ignoredPosition
    )) {
        lastSnapshotResult = "Resposta ONVIF sem URI de snapshot";
        return false;
    }

    snapshotUrl = xmlUnescape(snapshotUrl);
    snapshotUrl.trim();
    ParsedHttpResource parsedSnapshot;

    if (!parseHttpResourceUrl(snapshotUrl, parsedSnapshot)) {
        lastSnapshotResult = "Camera retornou um URI de snapshot invalido";
        return false;
    }

    if (parsedSnapshot.secure) {
        lastSnapshotResult = "Snapshot HTTPS local ainda nao suportado";
        return false;
    }

    snapshotTarget.valid = true;
    snapshotTarget.url = makeOnvifUrl(
        camera.ip,
        parsedSnapshot.port,
        parsedSnapshot.path
    );
    snapshotTarget.profileToken = profileToken;
    snapshotTarget.profileName = profileName;
    snapshotTargetFailureStreak = 0;
    lastSnapshotResult =
        currentTimestamp() + " - perfil " + snapshotTarget.profileName;

    return true;
}

void noteSnapshotRequestFailure(int statusCode)
{
    if (snapshotTargetFailureStreak < 255) {
        snapshotTargetFailureStreak++;
    }

    const bool permanentUriFailure = statusCode == 404 || statusCode == 410;
    const bool authenticationFailure = statusCode == 401 || statusCode == 403;
    const bool testingAlternateProfile =
        snapshotProfileFailoverPending &&
        !snapshotProfileTokenToSkip.isEmpty() &&
        snapshotTarget.profileToken != snapshotProfileTokenToSkip;

    if (testingAlternateProfile) {
        if (
            permanentUriFailure ||
            snapshotTargetFailureStreak >= SNAPSHOT_FAILURES_BEFORE_RERESOLVE
        ) {
            resetSnapshotProfileFailover();
        }

        return;
    }

    if (
        permanentUriFailure ||
        (
            !authenticationFailure &&
            snapshotTargetFailureStreak >= SNAPSHOT_FAILURES_BEFORE_RERESOLVE
        )
    ) {
        snapshotProfileTokenToSkip = snapshotTarget.profileToken;
        snapshotProfileFailoverPending = true;
        invalidateSnapshotTarget();
        lastSnapshotResolveAttemptAtMs = 0;
        snapshotTargetFailureStreak = 0;
    }
}

bool probeCameraSnapshotJpeg()
{
    SnapshotProbe result;
    result.attempted = true;
    snapshotProbeRuns++;
    const unsigned long startedAt = millis();
    bool resolutionAttempted = false;

    if (!snapshotTarget.valid) {
        if (
            lastSnapshotResolveAttemptAtMs != 0 &&
            millis() - lastSnapshotResolveAttemptAtMs <
                SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS
        ) {
            result.detail = "Aguardando nova tentativa de resolver o snapshot";
        } else {
            resolutionAttempted = true;

            if (!resolveSnapshotTarget()) {
                result.detail = lastSnapshotResult;
            }
        }
    }

    result.targetResolved = snapshotTarget.valid;

    if (!snapshotTarget.valid) {
        result.statusCode = lastSnapshotResolveStatusCode;
        result.httpResponded = result.statusCode >= 100;
        result.requestAttempted = resolutionAttempted;
        result.resolutionAttempted = resolutionAttempted;
        result.resolutionTransportFailure =
            resolutionAttempted && lastSnapshotResolveTransportFailure;
        result.conclusive =
            resolutionAttempted &&
            result.statusCode >= 500 &&
            result.statusCode <= 599;
        result.latencyMs = millis() - startedAt;

        if (resolutionAttempted) {
            snapshotProbeFailures++;
        }

        if (result.conclusive) {
            resetSnapshotProfileFailover();
        }

        lastSnapshotProbe = result;
        return false;
    }

    result.requestAttempted = true;
    result.conclusive = true;
    WiFiClient cameraClient;
    HTTPClient request;
    const int statusCode = openAuthenticatedCameraGet(
        request,
        cameraClient,
        snapshotTarget.url
    );

    result.statusCode = statusCode;
    result.latencyMs = millis() - startedAt;
    result.httpResponded = statusCode >= 100;

    if (statusCode < 200 || statusCode >= 300) {
        result.detail = statusCode < 0
            ? String("Falha HTTP: ") + HTTPClient::errorToString(statusCode)
            : String("Camera retornou HTTP ") + String(statusCode);
        request.end();
        noteSnapshotRequestFailure(statusCode);
        snapshotProbeFailures++;
        lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        lastSnapshotProbe = result;
        return false;
    }

    const int contentLength = request.getSize();

    if (
        contentLength == 0 ||
        (contentLength > 0 &&
         contentLength < static_cast<int>(MIN_VALID_SNAPSHOT_SIZE_BYTES)) ||
        contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)
    ) {
        result.detail = "Tamanho de snapshot invalido";
        request.end();
        noteSnapshotRequestFailure(statusCode);
        snapshotProbeFailures++;
        lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        lastSnapshotProbe = result;
        return false;
    }

    WiFiClient* source = request.getStreamPtr();

    if (source == nullptr) {
        result.detail = "Fluxo HTTP do snapshot indisponivel";
        request.end();
        noteSnapshotRequestFailure(statusCode);
        snapshotProbeFailures++;
        lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        lastSnapshotProbe = result;
        return false;
    }

    uint8_t buffer[SNAPSHOT_PROBE_BUFFER_SIZE];
    unsigned long lastDataAtMs = millis();
    uint8_t previousByte = 0;
    uint8_t firstByte = 0;
    uint8_t secondByte = 0;
    bool jpegStarted = false;
    size_t jpegEndOffset = 0;

    while (result.bytesRead < MAX_SNAPSHOT_SIZE_BYTES) {
        updateCameraAlertLed();
        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            size_t bytesToRead = min(availableBytes, sizeof(buffer));
            bytesToRead = min(
                bytesToRead,
                MAX_SNAPSHOT_SIZE_BYTES - result.bytesRead
            );

            if (contentLength > 0) {
                const size_t expectedBytes =
                    static_cast<size_t>(contentLength);
                const size_t remainingBytes = expectedBytes > result.bytesRead
                    ? expectedBytes - result.bytesRead
                    : 0;
                bytesToRead = min(bytesToRead, remainingBytes);
            }

            if (bytesToRead == 0) {
                break;
            }

            const size_t bytesRead = source->readBytes(buffer, bytesToRead);

            if (bytesRead == 0) {
                break;
            }

            for (size_t index = 0; index < bytesRead; index++) {
                const size_t absoluteIndex = result.bytesRead + index;
                const uint8_t value = buffer[index];

                if (absoluteIndex == 0) {
                    firstByte = value;
                } else if (absoluteIndex == 1) {
                    secondByte = value;
                    jpegStarted = firstByte == 0xFF && secondByte == 0xD8;
                }

                if (jpegStarted && previousByte == 0xFF && value == 0xD9) {
                    jpegEndOffset = absoluteIndex + 1;
                }

                previousByte = value;
            }

            result.bytesRead += bytesRead;
            lastDataAtMs = millis();

            if (
                contentLength < 0 &&
                jpegEndOffset > 0 &&
                result.bytesRead >= MIN_VALID_SNAPSHOT_SIZE_BYTES
            ) {
                break;
            }

            if (
                contentLength > 0 &&
                result.bytesRead >= static_cast<size_t>(contentLength)
            ) {
                break;
            }

            continue;
        }

        if (
            !source->connected() ||
            millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS
        ) {
            break;
        }

        delay(1);
    }

    request.end();
    result.latencyMs = millis() - startedAt;

    const bool completeLength =
        contentLength < 0 ||
        result.bytesRead == static_cast<size_t>(contentLength);
    const bool jpegEndNearTail =
        jpegEndOffset > 0 &&
        result.bytesRead >= jpegEndOffset &&
        result.bytesRead - jpegEndOffset <= 16;

    result.jpegValid =
        jpegStarted &&
        jpegEndNearTail &&
        completeLength &&
        result.bytesRead >= MIN_VALID_SNAPSHOT_SIZE_BYTES;

    if (result.jpegValid) {
        snapshotTargetFailureStreak = 0;
        resetSnapshotProfileFailover();
        result.detail =
            String("JPEG valido; ") + String(result.bytesRead) + " bytes";
        lastSnapshotResult =
            currentTimestamp() + " - " + result.detail + "; perfil " +
            snapshotTarget.profileName;
        lastSnapshotProbe = result;
        return true;
    }

    if (!jpegStarted) {
        result.detail = "Resposta nao iniciou com marcador JPEG FF D8";
    } else if (!completeLength) {
        result.detail =
            String("Transferencia incompleta; recebidos ") +
            String(result.bytesRead) + " de " + String(contentLength) +
            " bytes";
    } else if (jpegEndOffset == 0) {
        result.detail = "Snapshot incompleto; marcador FF D9 nao recebido";
    } else if (!jpegEndNearTail) {
        result.detail = "Marcador JPEG FF D9 apareceu antes do fim da resposta";
    } else {
        result.detail = "Snapshot JPEG menor que o limite minimo";
    }

    noteSnapshotRequestFailure(statusCode);
    snapshotProbeFailures++;
    lastSnapshotResult = currentTimestamp() + " - " + result.detail;
    lastSnapshotProbe = result;
    return false;
}

bool snapshotFailureAllowsAutomaticReboot()
{
    if (
        !lastSnapshotProbe.requestAttempted ||
        !lastSnapshotProbe.conclusive ||
        snapshotProfileFailoverPending
    ) {
        return false;
    }

    if (
        (
            lastSnapshotProbe.statusCode >= 400 &&
            lastSnapshotProbe.statusCode <= 499
        ) ||
        lastSnapshotProbe.statusCode == 501
    ) {
        return false;
    }

    return
        !containsIgnoreCase(lastSnapshotProbe.detail, "nenhum perfil") &&
        !containsIgnoreCase(lastSnapshotProbe.detail, "uri de snapshot invalido") &&
        !containsIgnoreCase(lastSnapshotProbe.detail, "https local ainda nao suportado");
}

bool requestCameraRebootOnvif(bool manualRequest = false)
{
    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        lastCameraRebootResult =
            currentTimestamp() + " - canal da camera ocupado pelo relay ao vivo";
        return false;
    }

    if (!camera.valid) {
        lastCameraRebootResult =
            currentTimestamp() + " - camera ainda nao localizada";
        return false;
    }

    if (!manualRequest && !ENABLE_ONVIF_AUTO_REBOOT) {
        lastCameraRebootResult =
            currentTimestamp() + " - reboot automatico desativado";
        return false;
    }

    refreshCameraRebootWindow();
    const unsigned long now = millis();

    if (!manualRequest && now - bootAtMs < STARTUP_GRACE_MS) {
        const unsigned long remainingMs =
            STARTUP_GRACE_MS - (now - bootAtMs);
        lastCameraRebootResult =
            currentTimestamp() +
            " - reboot aguardando a tolerancia inicial; faltam " +
            durationText(remainingMs);
        return false;
    }

    if (cameraRebootGraceActive()) {
        return false;
    }

    if (
        lastCameraRebootAttemptAtMs != 0 &&
        now - lastCameraRebootAttemptAtMs < ONVIF_REBOOT_COOLDOWN_MS
    ) {
        const unsigned long remainingMs =
            ONVIF_REBOOT_COOLDOWN_MS -
            (now - lastCameraRebootAttemptAtMs);
        lastCameraRebootResult =
            currentTimestamp() + " - novo reboot bloqueado por cooldown; faltam " +
            durationText(remainingMs);
        return false;
    }

    if (
        cameraRebootAttemptsInWindow >=
        MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW
    ) {
        const unsigned long elapsedMs =
            now - cameraRebootWindowStartedAtMs;
        const unsigned long remainingMs = elapsedMs < ONVIF_REBOOT_WINDOW_MS
            ? ONVIF_REBOOT_WINDOW_MS - elapsedMs
            : 0;
        lastCameraRebootResult =
            currentTimestamp() +
            " - limite de tentativas aceitas atingido; nova janela em " +
            durationText(remainingMs);
        return false;
    }

    lastCameraRebootAttemptAtMs = now;
    cameraRebootAttempts++;

    String responseBody;
    int statusCode = 0;
    const bool accepted = postOnvifSoap(
        camera.onvifUrl,
        "http://www.onvif.org/ver10/device/wsdl/SystemReboot",
        "<tds:SystemReboot/>",
        responseBody,
        false,
        &statusCode
    );

    if (!accepted) {
        cameraRebootErrors++;
        lastCameraRebootResult = currentTimestamp() + " - rejeitado; ";

        if (statusCode < 0) {
            lastCameraRebootResult += HTTPClient::errorToString(statusCode);
        } else {
            lastCameraRebootResult += "HTTP ";
            lastCameraRebootResult += statusCode;
        }

        String faultText;
        int ignoredPosition = 0;
        extractLocalTag(
            responseBody,
            "Text",
            0,
            faultText,
            ignoredPosition
        );

        if (!faultText.isEmpty()) {
            lastCameraRebootResult += "; ";
            lastCameraRebootResult += faultText;
        }

        if (statusCode == 401 && !cameraCredentialsConfigured()) {
            lastCameraRebootResult +=
                "; configure CAMERA_ONVIF_USERNAME_VALUE e CAMERA_ONVIF_PASSWORD_VALUE";
        }

        Serial.println(lastCameraRebootResult);
        return false;
    }

    cameraRebootAccepted++;
    const unsigned long acceptedAtMs = millis();
    cameraRebootAcceptedAtMs[cameraRebootAttemptsInWindow++] = acceptedAtMs;
    cameraRebootWindowStartedAtMs = cameraRebootAcceptedAtMs[0];
    cameraRebootGraceStartedAtMs = acceptedAtMs;
    invalidateSnapshotTarget();
    invalidateOnvifServiceCatalog();
    resetSnapshotProfileFailover();
    invalidateLiveProfiles();
    lastSnapshotResolveAttemptAtMs = now;
    snapshotTargetFailureStreak = 0;
    consecutiveFailures = 0;
    consecutiveSuccesses = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    consecutiveIndependentMediaFailures = 0;
    lastSnapshotResolveStatusCode = 0;
    lastSnapshotResolveTransportFailure = false;
    lastSnapshotProbe = SnapshotProbe{};
    lastSnapshotProbe.attempted = true;
    lastSnapshotProbe.detail = "Aguardando a camera reiniciar";

    String rebootMessage;
    int ignoredPosition = 0;
    extractLocalTag(
        responseBody,
        "Message",
        0,
        rebootMessage,
        ignoredPosition
    );

    lastCameraRebootResult =
        currentTimestamp() + " - aceito; HTTP " + String(statusCode);

    if (!rebootMessage.isEmpty()) {
        lastCameraRebootResult += "; ";
        lastCameraRebootResult += rebootMessage;
    }

    setState(
        CameraState::Recovering,
        "Reinicializacao ONVIF solicitada; aguardando a camera"
    );
    Serial.println(lastCameraRebootResult);
    return true;
}

void sendSnapshotError(
    int statusCode,
    const char* message,
    bool countError = true
)
{
    if (countError) {
        snapshotErrors++;
    }

    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(
        statusCode,
        "application/json; charset=utf-8",
        String("{\"error\":\"") + message + "\"}"
    );
}

void serveCameraSnapshot()
{
    if (healthCheckInProgress) {
        previewBusyResponses++;
        sendSnapshotError(503, "health_check_in_progress", false);
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        previewBusyResponses++;
        sendSnapshotError(503, "camera_io_busy", false);
        return;
    }

    const unsigned long snapshotStartedAtMs = millis();

    if (!snapshotTarget.valid) {
        if (
            lastSnapshotResolveAttemptAtMs != 0 &&
            millis() - lastSnapshotResolveAttemptAtMs <
                SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS
        ) {
            sendSnapshotError(503, "snapshot_temporarily_unavailable");
            return;
        }

        if (!resolveSnapshotTarget()) {
            sendSnapshotError(503, "snapshot_unavailable");
            return;
        }
    }

    WiFiClient cameraClient;
    HTTPClient request;
    previewCameraFetches++;
    const int statusCode = openAuthenticatedCameraGet(
        request,
        cameraClient,
        snapshotTarget.url
    );
    const int contentLength = request.getSize();

    if (
        statusCode < 200 ||
        statusCode >= 300 ||
        contentLength == 0 ||
        (contentLength > 0 &&
         contentLength < static_cast<int>(MIN_VALID_SNAPSHOT_SIZE_BYTES)) ||
        contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)
    ) {
        request.end();
        lastSnapshotResult = statusCode < 0
            ? currentTimestamp() + " - " + HTTPClient::errorToString(statusCode)
            : currentTimestamp() + " - falha HTTP " + String(statusCode);
        sendSnapshotError(502, "invalid_camera_response");
        return;
    }

    WiFiClient* source = request.getStreamPtr();

    if (source == nullptr) {
        request.end();
        sendSnapshotError(502, "camera_stream_unavailable");
        return;
    }

    uint8_t prefix[2] = {0, 0};
    size_t prefixLength = 0;
    unsigned long lastDataAtMs = millis();

    while (prefixLength < sizeof(prefix)) {
        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            const int value = source->read();

            if (value < 0) {
                break;
            }

            prefix[prefixLength++] = static_cast<uint8_t>(value);
            lastDataAtMs = millis();
            continue;
        }

        if (
            !source->connected() ||
            millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS
        ) {
            break;
        }

        updateCameraAlertLed();
        delay(1);
    }

    if (
        prefixLength != sizeof(prefix) ||
        prefix[0] != 0xFF ||
        prefix[1] != 0xD8
    ) {
        request.end();
        lastSnapshotResult = "Resposta da camera nao e um JPEG valido";
        sendSnapshotError(502, "invalid_jpeg_header");
        return;
    }

    httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    httpServer.sendHeader("Pragma", "no-cache");
    httpServer.sendHeader("X-Content-Type-Options", "nosniff");

    if (contentLength > 0) {
        httpServer.setContentLength(static_cast<size_t>(contentLength));
    } else {
        httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    }

    httpServer.send(200, "image/jpeg", "");
    httpServer.sendContent(
        reinterpret_cast<const char*>(prefix),
        sizeof(prefix)
    );

    WiFiClient browserClient = httpServer.client();
    uint8_t relayBuffer[SNAPSHOT_RELAY_BUFFER_SIZE];
    size_t bytesRelayed = sizeof(prefix);
    uint8_t previousByte = prefix[1];
    size_t jpegEndOffset = 0;
    bool browserDisconnected = false;
    lastDataAtMs = millis();

    while (bytesRelayed < MAX_SNAPSHOT_SIZE_BYTES) {
        updateCameraAlertLed();

        if (!browserClient.connected()) {
            browserDisconnected = true;
            break;
        }

        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            size_t bytesToRead = min(availableBytes, sizeof(relayBuffer));

            if (contentLength > 0) {
                const size_t remaining =
                    static_cast<size_t>(contentLength) - bytesRelayed;
                bytesToRead = min(bytesToRead, remaining);
            } else {
                bytesToRead = min(
                    bytesToRead,
                    MAX_SNAPSHOT_SIZE_BYTES - bytesRelayed
                );
            }

            if (bytesToRead == 0) {
                break;
            }

            const size_t bytesRead = source->readBytes(
                relayBuffer,
                bytesToRead
            );

            if (bytesRead == 0) {
                break;
            }

            for (size_t index = 0; index < bytesRead; index++) {
                const uint8_t value = relayBuffer[index];

                if (previousByte == 0xFF && value == 0xD9) {
                    jpegEndOffset = bytesRelayed + index + 1;
                }

                previousByte = value;
            }

            httpServer.sendContent(
                reinterpret_cast<const char*>(relayBuffer),
                bytesRead
            );
            bytesRelayed += bytesRead;
            lastDataAtMs = millis();

            if (
                contentLength > 0 &&
                bytesRelayed >= static_cast<size_t>(contentLength)
            ) {
                break;
            }

            if (contentLength < 0 && jpegEndOffset > 0) {
                break;
            }

            continue;
        }

        if (
            !source->connected() ||
            millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS
        ) {
            break;
        }

        delay(1);
    }

    request.end();

    if (contentLength < 0 && browserClient.connected()) {
        httpServer.sendContent("");
    }

    const bool completeLength =
        contentLength < 0 ||
        bytesRelayed == static_cast<size_t>(contentLength);
    const bool jpegEndNearTail =
        jpegEndOffset > 0 &&
        bytesRelayed >= jpegEndOffset &&
        bytesRelayed - jpegEndOffset <= 16;
    const bool validTransfer =
        !browserDisconnected &&
        completeLength &&
        jpegEndNearTail &&
        bytesRelayed >= MIN_VALID_SNAPSHOT_SIZE_BYTES;

    if (validTransfer) {
        snapshotsServed++;
        previewBytesRelayed += bytesRelayed;
        lastBrowserSnapshotDeliveredAtMs = millis();
        lastBrowserSnapshotBytes = bytesRelayed;
        lastBrowserSnapshotLatencyMs =
            lastBrowserSnapshotDeliveredAtMs - snapshotStartedAtMs;
        lastSnapshotResult =
            currentTimestamp() + " - quadro entregue; " +
            String(bytesRelayed) + " bytes; perfil " +
            snapshotTarget.profileName;
    } else if (browserDisconnected) {
        previewClientAborts++;
    } else {
        snapshotErrors++;
        lastSnapshotResult = "Transferencia do snapshot JPEG incompleta";
    }
}

bool parseSafeRtspProbeTarget(
    const String& uri,
    const IPAddress& expectedIp,
    uint16_t& port
)
{
    if (!uri.startsWith("rtsp://")) {
        return false;
    }

    const int authorityStart = 7;
    const int pathStart = uri.indexOf('/', authorityStart);
    const int authorityEnd = pathStart >= 0 ? pathStart : uri.length();
    String authority = uri.substring(authorityStart, authorityEnd);

    if (
        authority.isEmpty() ||
        authority.indexOf('@') >= 0 ||
        uri.indexOf('\r') >= 0 ||
        uri.indexOf('\n') >= 0
    ) {
        return false;
    }

    port = 554;
    const int colonPosition = authority.lastIndexOf(':');

    if (colonPosition > 0) {
        const String portText = authority.substring(colonPosition + 1);

        for (size_t index = 0; index < portText.length(); index++) {
            if (!isdigit(static_cast<unsigned char>(portText.charAt(index)))) {
                return false;
            }
        }

        const long parsedPort = portText.toInt();

        if (parsedPort <= 0 || parsedPort > 65535) {
            return false;
        }

        port = static_cast<uint16_t>(parsedPort);
        authority = authority.substring(0, colonPosition);
    }

    IPAddress advertisedIp;
    return
        advertisedIp.fromString(authority.c_str()) &&
        advertisedIp == expectedIp;
}

ServiceProbe probeRtsp(const IPAddress& ip, uint16_t port)
{
    ServiceProbe result;
    String requestUri = String("rtsp://") + ip.toString() + ':' +
        String(port) + '/';
    const char* method = "OPTIONS";
    uint16_t targetPort = port;
    LiveStreamDescriptor descriptor;
    String descriptorError;
    size_t preferredProfileIndex = 0;

    if (
        camera.valid &&
        camera.ip == ip &&
        findPreferredLiveProfileIndex(preferredProfileIndex) &&
        provideLiveStreamDescriptor(
            preferredProfileIndex,
            descriptor,
            descriptorError,
            SERVICE_RESPONSE_TIMEOUT_MS
        )
    ) {
        uint16_t descriptorPort = 0;

        if (parseSafeRtspProbeTarget(
            descriptor.uri,
            ip,
            descriptorPort
        )) {
            requestUri = descriptor.uri;
            targetPort = descriptorPort;
            method = "DESCRIBE";
            result.mediaRouteTested = true;
        }
    }

    WiFiClient client;
    const unsigned long startedAt = millis();

    if (!client.connect(ip, targetPort, TCP_CONNECT_TIMEOUT_MS)) {
        result.latencyMs = millis() - startedAt;
        return result;
    }

    result.tcpConnected = true;
    result.latencyMs = millis() - startedAt;

    client.print(method);
    client.print(' ');
    client.print(requestUri);
    client.print(" RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: ESP32-Camera-Watchdog/");
    client.print(FIRMWARE_VERSION);

    if (result.mediaRouteTested) {
        client.print("\r\nAccept: application/sdp");
    }

    client.print("\r\n\r\n");

    const unsigned long responseStartedAt = millis();
    String firstLine;
    firstLine.reserve(96);

    while (millis() - responseStartedAt < SERVICE_RESPONSE_TIMEOUT_MS) {
        updateCameraAlertLed();

        while (client.available()) {
            const char value = static_cast<char>(client.read());

            if (value == '\n') {
                break;
            }

            if (value != '\r' && firstLine.length() < 200) {
                firstLine += value;
            }
        }

        if (!firstLine.isEmpty() && firstLine.indexOf("RTSP/") == 0) {
            result.protocolResponded = true;
            const int firstSpace = firstLine.indexOf(' ');

            if (firstSpace >= 0) {
                result.statusCode = firstLine.substring(firstSpace + 1).toInt();
            }

            break;
        }

        if (!client.connected() && !client.available()) {
            break;
        }

        httpServer.handleClient();
        delay(1);
    }

    client.stop();
    result.latencyMs = millis() - startedAt;
    return result;
}

bool tcpPortOpen(
    const IPAddress& ip,
    uint16_t port,
    uint32_t timeoutMs
)
{
    WiFiClient client;
    const bool connected = client.connect(ip, port, timeoutMs);
    client.stop();
    return connected;
}

bool scanSubnetForCamera()
{
    subnetScanRuns++;
    lastSubnetScanAtMs = millis();
    lastEvent = currentTimestamp() + " - Varrendo a sub-rede por RTSP e ONVIF";
    Serial.println(lastEvent);

    const uint32_t local = ipToUint32(WiFi.localIP());
    const uint32_t mask = ipToUint32(WiFi.subnetMask());
    const uint32_t network = local & mask;
    const uint32_t broadcast = network | (~mask);

    if (broadcast <= network + 1) {
        lastScanResult = "Mascara de rede invalida para varredura";
        return false;
    }

    const uint32_t hostCount = broadcast - network - 1;

    if (hostCount > MAX_SCAN_HOSTS) {
        lastScanResult =
            "Rede possui " +
            String(hostCount) +
            " hosts; limite configurado=" +
            String(MAX_SCAN_HOSTS);

        Serial.println(lastScanResult);
        return false;
    }

    const IPAddress ownIp = WiFi.localIP();
    const IPAddress gateway = WiFi.gatewayIP();

    Serial.printf(
        "Varredura de %s ate %s (%lu hosts)\n",
        uint32ToIp(network + 1).toString().c_str(),
        uint32ToIp(broadcast - 1).toString().c_str(),
        static_cast<unsigned long>(hostCount)
    );

    for (uint32_t value = network + 1; value < broadcast; value++) {
        updateCameraAlertLed();
        const IPAddress candidateIp = uint32ToIp(value);

        if (candidateIp == ownIp || candidateIp == gateway) {
            continue;
        }

        httpServer.handleClient();

        if (!tcpPortOpen(
            candidateIp,
            CAMERA_RTSP_PORT,
            SCAN_CONNECT_TIMEOUT_MS
        )) {
            delay(1);
            continue;
        }

        Serial.printf(
            "Porta RTSP aberta em %s\n",
            candidateIp.toString().c_str()
        );

        for (size_t portIndex = 0;
             portIndex < ONVIF_PORT_CANDIDATE_COUNT;
             portIndex++) {
            const uint16_t onvifPort = ONVIF_PORT_CANDIDATES[portIndex];
            CameraTarget candidate;
            candidate.valid = true;
            candidate.ip = candidateIp;
            candidate.onvifPort = onvifPort;
            candidate.rtspPort = CAMERA_RTSP_PORT;
            candidate.onvifPath = DEFAULT_ONVIF_PATH;
            candidate.onvifUrl = makeOnvifUrl(
                candidate.ip,
                candidate.onvifPort,
                candidate.onvifPath
            );

            const ServiceProbe candidateProbe = probeOnvif(candidate);

            if (!candidateProbe.protocolResponded) {
                continue;
            }

            const String oldIp = camera.valid ? camera.ip.toString() : "";
            camera = candidate;
            camera.endpointUuid = "";
            camera.scopes = "Localizada por varredura RTSP/ONVIF";
            lastOnvifProbe = candidateProbe;
            invalidateSnapshotTarget();
            invalidateOnvifServiceCatalog();
            resetSnapshotProfileFailover();
            invalidateLiveProfiles();
            lastSnapshotResolveAttemptAtMs = 0;
            consecutiveFailures = 0;
            consecutiveSuccesses = 0;
            consecutiveControlFailures = 0;
            consecutiveInconclusiveChecks = 0;
            consecutiveMediaServiceFailures = 0;
            consecutiveIndependentMediaFailures = 0;
            snapshotTargetFailureStreak = 0;
            saveCameraTarget();

            lastScanResult =
                currentTimestamp() +
                " - camera candidata em " +
                camera.onvifUrl;

            Serial.println(lastScanResult);

            if (!oldIp.isEmpty() && oldIp != camera.ip.toString()) {
                sendIpChangeAlert(oldIp, camera.ip.toString());
            }

            return true;
        }
    }

    lastScanResult = currentTimestamp() + " - nenhuma camera localizada";
    Serial.println(lastScanResult);
    return false;
}

bool cameraControlResponded()
{
    if (!camera.valid) {
        lastOnvifProbe = ServiceProbe{};
        lastRtspProbe = ServiceProbe{};
        return false;
    }

    lastOnvifProbe = probeOnvif(camera);
    httpServer.handleClient();
    lastRtspProbe = probeRtsp(camera.ip, camera.rtspPort);

    return
        lastOnvifProbe.protocolResponded ||
        lastRtspProbe.protocolResponded;
}

void resetCameraCheckSequences(const char* snapshotDetail)
{
    consecutiveFailures = 0;
    consecutiveSuccesses = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    consecutiveIndependentMediaFailures = 0;
    snapshotTargetFailureStreak = 0;
    resetSnapshotProfileFailover();
    lastOnvifProbe = ServiceProbe{};
    lastRtspProbe = ServiceProbe{};
    lastSnapshotProbe = SnapshotProbe{};
    lastSnapshotProbe.detail = snapshotDetail;
    lastBrowserSnapshotDeliveredAtMs = 0;
    lastBrowserSnapshotBytes = 0;
    lastBrowserSnapshotLatencyMs = 0;
    lastRelayMediaEvidenceConsumedAtMs = 0;
    lastHealthEvidenceSource = HealthEvidenceSource::None;
    lastHealthEvidenceAtMs = 0;
    resetMediaCanaryFailureEvidence();
    liveStreamRelay.invalidateMediaEvidence();
}

bool liveRelayHasRecentMedia(const LiveRelayStats& stats)
{
    return
        stats.lastMediaPacketAtMs != 0 &&
        millis() - stats.lastMediaPacketAtMs <=
            LIVE_MEDIA_HEALTH_EVIDENCE_MS;
}

bool browserSnapshotRecentlyDelivered()
{
    return
        lastBrowserSnapshotDeliveredAtMs != 0 &&
        millis() - lastBrowserSnapshotDeliveredAtMs <=
            HEALTH_CHECK_INTERVAL_MS;
}

bool mediaCanaryEvidenceRecentlyValidated()
{
    return
        lastHealthEvidenceSource == HealthEvidenceSource::MediaCanaryRtp &&
        lastHealthEvidenceAtMs != 0 &&
        millis() - lastHealthEvidenceAtMs <=
            LIVE_MEDIA_CANARY_REUSE_MS;
}

void noteInconclusiveHealthCheck(const String& event)
{
    consecutiveIndependentMediaFailures = 0;
    consecutiveSuccesses = 0;

    if (
        cameraState == CameraState::Online &&
        activeIncident == IncidentKind::None
    ) {
        lastEvent = currentTimestamp() + " - " + event;
        Serial.printf("VERIFICACAO ADIADA: %s\n", event.c_str());
        return;
    }

    setState(CameraState::Degraded, event);
}

void recordSuccessfulHealthCheck(
    HealthEvidenceSource evidenceSource,
    const char* confirmationEvent,
    const char* healthyEvent,
    const char* recoveredEvent
)
{
    successfulHealthChecks++;
    lastHealthEvidenceSource = evidenceSource;
    lastHealthEvidenceAtMs = millis();
    consecutiveFailures = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    consecutiveIndependentMediaFailures = 0;
    clearMediaCanaryFailureEvidence();

    if (consecutiveSuccesses < 255) {
        consecutiveSuccesses++;
    }

    cameraRebootGraceStartedAtMs = 0;

    if (
        activeIncident != IncidentKind::None &&
        consecutiveSuccesses < SUCCESSES_BEFORE_RECOVERY
    ) {
        setState(CameraState::Degraded, confirmationEvent);
        return;
    }

    const bool recovered = activeIncident != IncidentKind::None;
    resolveActiveIncident();
    setState(
        CameraState::Online,
        recovered ? recoveredEvent : healthyEvent
    );
}

MediaCanaryOutcome runIndependentMediaCanary()
{
    size_t profileIndices[MAX_LIVE_PROFILES];
    const size_t profileCount = collectCompatibleLiveProfileIndices(
        profileIndices,
        MAX_LIVE_PROFILES
    );
    mediaCanaryCycles++;
    lastMediaCanaryProfilesTested = 0;

    if (profileCount == 0) {
        resetMediaCanaryFailureEvidence();
        lastMediaCanaryProbe = LiveMediaProbeResult{};
        copySettingText(
            lastMediaCanaryProbe.detail,
            "Catalogo sem perfil H.264 para sonda RTP"
        );
        mediaCanaryInconclusive++;
        return MediaCanaryOutcome::Inconclusive;
    }

    uint16_t requiredProfilesMask = 0;
    const unsigned long evidenceNowMs = millis();
    const unsigned long evidenceMaxAgeMs =
        HEALTH_CHECK_INTERVAL_MS *
        static_cast<unsigned long>(profileCount + 1U);

    for (size_t index = 0; index < profileCount; index++) {
        const size_t profileIndex = profileIndices[index];
        const uint16_t profileBit =
            static_cast<uint16_t>(1U << profileIndex);
        requiredProfilesMask |= profileBit;

        if (
            (mediaCanarySilentProfilesMask & profileBit) != 0 &&
            (
                mediaCanarySilentProfileAtMs[profileIndex] == 0 ||
                evidenceNowMs - mediaCanarySilentProfileAtMs[profileIndex] >
                    evidenceMaxAgeMs
            )
        ) {
            mediaCanarySilentProfilesMask &=
                static_cast<uint16_t>(~profileBit);
            mediaCanarySilentProfileAtMs[profileIndex] = 0;
        }
    }

    const uint32_t cycleStartedAtMs = millis();
    const size_t startOffset = mediaCanaryNextProfileOffset % profileCount;
    bool sawInconclusiveProfile = false;

    for (size_t tested = 0; tested < profileCount; tested++) {
        const size_t index = (startOffset + tested) % profileCount;
        const uint32_t cycleElapsedMs = millis() - cycleStartedAtMs;

        if (cycleElapsedMs >= LIVE_MEDIA_PROBE_CYCLE_TIMEOUT_MS) {
            if (lastMediaCanaryProfilesTested == 0) {
                lastMediaCanaryProbe = LiveMediaProbeResult{};
            }
            copySettingText(
                lastMediaCanaryProbe.detail,
                "Sonda RTP interrompida pelo limite total do ciclo"
            );
            mediaCanarySweepPending++;
            break;
        }

        const uint32_t cycleRemainingMs =
            LIVE_MEDIA_PROBE_CYCLE_TIMEOUT_MS - cycleElapsedMs;

        if (cycleRemainingMs < LIVE_MEDIA_PROBE_TIMEOUT_MS) {
            if (lastMediaCanaryProfilesTested == 0) {
                lastMediaCanaryProbe = LiveMediaProbeResult{};
            }
            copySettingText(
                lastMediaCanaryProbe.detail,
                "Sonda RTP aguardando orcamento integral no proximo ciclo"
            );
            mediaCanarySweepPending++;
            break;
        }

        updateCameraAlertLed();
        mediaCanaryRuns++;
        lastMediaCanaryAtMs = millis();
        lastMediaCanaryProfileIndex = profileIndices[index];
        lastMediaCanaryProfilesTested++;
        const bool mediaReceived = liveStreamRelay.probeMedia(
            profileIndices[index],
            LIVE_MEDIA_PROBE_TIMEOUT_MS,
            lastMediaCanaryProbe
        );

        if (mediaReceived) {
            mediaCanarySuccesses++;
            resetMediaCanaryFailureEvidence();
            mediaCanaryNextProfileOffset = index;
            return MediaCanaryOutcome::MediaReceived;
        }

        const bool conclusiveMediaFailure =
            lastMediaCanaryProbe.descriptorResolved &&
            lastMediaCanaryProbe.playAccepted &&
            !lastMediaCanaryProbe.mediaReceived &&
            lastMediaCanaryProbe.observedAfterPlayMs >=
                LIVE_MEDIA_PROBE_MIN_OBSERVATION_MS;

        if (conclusiveMediaFailure) {
            mediaCanaryFailures++;
            const size_t profileIndex = profileIndices[index];
            const uint16_t profileBit =
                static_cast<uint16_t>(1U << profileIndex);
            mediaCanarySilentProfilesMask |= profileBit;
            mediaCanarySilentProfileAtMs[profileIndex] = millis();
        } else {
            mediaCanaryInconclusive++;
            sawInconclusiveProfile = true;
            const size_t profileIndex = profileIndices[index];
            const uint16_t profileBit =
                static_cast<uint16_t>(1U << profileIndex);
            mediaCanarySilentProfilesMask &=
                static_cast<uint16_t>(~profileBit);
            mediaCanarySilentProfileAtMs[profileIndex] = 0;
        }
    }

    mediaCanaryNextProfileOffset =
        (startOffset + lastMediaCanaryProfilesTested) % profileCount;

    if (
        (mediaCanarySilentProfilesMask & requiredProfilesMask) ==
            requiredProfilesMask
    ) {
        clearMediaCanaryFailureEvidence();
        return MediaCanaryOutcome::AllProfilesSilentAfterPlay;
    }

    if (sawInconclusiveProfile) {
        clearMediaCanaryFailureEvidence();
        return MediaCanaryOutcome::Inconclusive;
    }

    return MediaCanaryOutcome::SweepPending;
}

struct HealthCheckScheduleGuard {
    HealthCheckScheduleGuard()
    {
        healthCheckInProgress = true;
    }

    ~HealthCheckScheduleGuard()
    {
        healthCheckInProgress = false;
        lastHealthCheckAtMs = millis();
    }
};

void runHealthCheck()
{
    HealthCheckScheduleGuard scheduleGuard;

    if (
        cameraRebootGraceStartedAtMs != 0 &&
        !cameraRebootGraceActive()
    ) {
        cameraRebootGraceStartedAtMs = 0;
    }

    if (WiFi.status() != WL_CONNECTED) {
        totalHealthChecks++;
        failedHealthChecks++;
        resetCameraCheckSequences("ESP32 sem conexao Wi-Fi");
        setState(CameraState::Degraded, "ESP32 perdeu a conexao Wi-Fi");
        return;
    }

    if (cameraRebootGraceActive()) {
        setState(
            CameraState::Recovering,
            String("Aguardando reinicializacao da camera; faltam cerca de ") +
                String(cameraRebootGraceRemainingMs() / 1000UL) + "s"
        );
        return;
    }

    LiveRelayStats relayStats;
    liveStreamRelay.snapshotStats(relayStats);

    if (
        liveRelayHasRecentMedia(relayStats) &&
        relayStats.lastMediaPacketAtMs !=
            lastRelayMediaEvidenceConsumedAtMs
    ) {
        totalHealthChecks++;
        lastRelayMediaEvidenceConsumedAtMs = relayStats.lastMediaPacketAtMs;
        Serial.printf(
            "CAMERA OK: fluxo RTP entregue ha %lus; probes paralelos adiados.\n",
            static_cast<unsigned long>(
                (millis() - relayStats.lastMediaPacketAtMs) / 1000UL
            )
        );
        recordSuccessfulHealthCheck(
            HealthEvidenceSource::RelayRtp,
            "Fluxo RTP retornou; confirmando estabilidade",
            "Camera entregando fluxo RTP ao vivo",
            "Camera recuperada e fluxo RTP validado"
        );
        return;
    }

    if (relayStats.clientActive) {
        totalHealthChecks++;
        inconclusiveHealthChecks++;
        clearMediaCanaryFailureEvidence();
        noteInconclusiveHealthCheck(
            "Sessao ao vivo em negociacao; probes paralelos adiados"
        );
        return;
    }

    if (browserSnapshotRecentlyDelivered()) {
        totalHealthChecks++;
        previewHealthEvidenceUses++;
        lastSnapshotProbe = SnapshotProbe{};
        lastSnapshotProbe.attempted = true;
        lastSnapshotProbe.conclusive = true;
        lastSnapshotProbe.targetResolved = snapshotTarget.valid;
        lastSnapshotProbe.httpResponded = true;
        lastSnapshotProbe.jpegValid = true;
        lastSnapshotProbe.statusCode = 200;
        lastSnapshotProbe.latencyMs = lastBrowserSnapshotLatencyMs;
        lastSnapshotProbe.bytesRead = lastBrowserSnapshotBytes;
        lastSnapshotProbe.detail = "JPEG validado pela Previa recente";
        recordSuccessfulHealthCheck(
            HealthEvidenceSource::PreviewJpeg,
            "Previa retornou; confirmando estabilidade",
            "Camera validada pela Previa recente",
            "Camera recuperada e JPEG validado pela Previa"
        );
        return;
    }

    if (liveRelayHasRecentMedia(relayStats)) {
        totalHealthChecks++;
        inconclusiveHealthChecks++;
        consecutiveFailures = 0;
        consecutiveInconclusiveChecks = 0;
        consecutiveMediaServiceFailures = 0;
        clearMediaCanaryFailureEvidence();
        noteInconclusiveHealthCheck(
            "Fluxo RTP recente; probes adiados durante a graca pos-sessao"
        );
        return;
    }

    if (
        activeIncident == IncidentKind::None &&
        mediaCanaryEvidenceRecentlyValidated()
    ) {
        totalHealthChecks++;
        inconclusiveHealthChecks++;
        consecutiveFailures = 0;
        consecutiveInconclusiveChecks = 0;
        consecutiveMediaServiceFailures = 0;
        clearMediaCanaryFailureEvidence();
        noteInconclusiveHealthCheck(
            "RTP canario recente; novas sondas adiadas"
        );
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        totalHealthChecks++;
        inconclusiveHealthChecks++;
        clearMediaCanaryFailureEvidence();
        noteInconclusiveHealthCheck(
            "Canal da camera ocupado pelo relay ao vivo; verificacao adiada"
        );
        return;
    }

    totalHealthChecks++;

    if (!camera.valid) {
        discoverOnvifCamera();
    }

    bool controlAvailable = cameraControlResponded();
    const uint8_t nextControlFailure =
        consecutiveControlFailures < 255
            ? consecutiveControlFailures + 1
            : consecutiveControlFailures;

    if (
        !controlAvailable &&
        nextControlFailure >= FAILURES_BEFORE_REDISCOVERY &&
        (
            lastRediscoveryAtMs == 0 ||
            millis() - lastRediscoveryAtMs >= REDISCOVERY_INTERVAL_MS
        )
    ) {
        const bool rediscovered = discoverOnvifCamera();

        if (rediscovered) {
            controlAvailable = cameraControlResponded();
        }
    }

    if (
        !controlAvailable &&
        nextControlFailure >= FAILURES_BEFORE_OFFLINE &&
        ENABLE_AUTOMATIC_SUBNET_SCAN &&
        (
            lastSubnetScanAtMs == 0 ||
            millis() - lastSubnetScanAtMs >= SUBNET_SCAN_INTERVAL_MS
        )
    ) {
        const bool foundByScan = scanSubnetForCamera();

        if (foundByScan) {
            controlAvailable = cameraControlResponded();
        }
    }

    const bool videoAvailable = probeCameraSnapshotJpeg();

    if (videoAvailable) {
        Serial.printf(
            "CAMERA OK: %s | JPEG=%u bytes %lums | "
            "ONVIF tcp=%d http=%d code=%d | "
            "RTSP tcp=%d protocolo=%d code=%d\n",
            camera.ip.toString().c_str(),
            static_cast<unsigned>(lastSnapshotProbe.bytesRead),
            static_cast<unsigned long>(lastSnapshotProbe.latencyMs),
            lastOnvifProbe.tcpConnected,
            lastOnvifProbe.protocolResponded,
            lastOnvifProbe.statusCode,
            lastRtspProbe.tcpConnected,
            lastRtspProbe.protocolResponded,
            lastRtspProbe.statusCode
        );

        recordSuccessfulHealthCheck(
            HealthEvidenceSource::SnapshotJpeg,
            "Imagem retornou; confirmando estabilidade",
            "Camera e canal de imagem responderam ao teste",
            "Camera recuperada e JPEG validado"
        );
        return;
    }

    const bool mediaServiceTransportFailure =
        !lastSnapshotProbe.conclusive &&
        controlAvailable &&
        lastSnapshotProbe.requestAttempted &&
        lastSnapshotProbe.resolutionAttempted &&
        lastSnapshotProbe.resolutionTransportFailure &&
        lastOnvifProbe.protocolResponded;

    if (!lastSnapshotProbe.conclusive && controlAvailable) {

        if (mediaServiceTransportFailure) {
            if (consecutiveMediaServiceFailures < 255) {
                consecutiveMediaServiceFailures++;
            }
        } else if (lastSnapshotProbe.requestAttempted) {
            consecutiveMediaServiceFailures = 0;
        }

        if (
            mediaServiceTransportFailure &&
            consecutiveMediaServiceFailures >= VIDEO_FAILURES_BEFORE_REBOOT
        ) {
            lastSnapshotProbe.conclusive = true;
            lastSnapshotProbe.detail =
                String("Servico de midia ONVIF falhou repetidamente: ") +
                lastSnapshotProbe.detail;
            resetSnapshotProfileFailover();
        }
    }

    if (!lastSnapshotProbe.conclusive && controlAvailable) {
        consecutiveControlFailures = 0;

        if (
            lastSnapshotProbe.requestAttempted &&
            consecutiveInconclusiveChecks < 255
        ) {
            consecutiveInconclusiveChecks++;
        }

        const bool shouldTryMediaCanary =
            lastSnapshotProbe.requestAttempted &&
            millis() - bootAtMs >= STARTUP_GRACE_MS &&
            consecutiveInconclusiveChecks >= VIDEO_FAILURES_BEFORE_ALERT;
        MediaCanaryOutcome mediaCanaryOutcome =
            MediaCanaryOutcome::Inconclusive;

        if (shouldTryMediaCanary) {
            mediaCanaryOutcome = runIndependentMediaCanary();
        }

        if (
            mediaCanaryOutcome == MediaCanaryOutcome::MediaReceived
        ) {
            recordSuccessfulHealthCheck(
                HealthEvidenceSource::MediaCanaryRtp,
                "RTP canario retornou; confirmando estabilidade",
                "Camera validada por RTP canario",
                "Camera recuperada e RTP canario validado"
            );
            return;
        }

        inconclusiveHealthChecks++;

        if (
            shouldTryMediaCanary &&
            mediaCanaryOutcome ==
                MediaCanaryOutcome::AllProfilesSilentAfterPlay
        ) {
            beginOrEscalateIncident(IncidentKind::SnapshotUnavailable);
            queueIncidentAlert();
        }

        noteInconclusiveHealthCheck(
            String("Monitor de snapshot inconclusivo: ") +
                lastSnapshotProbe.detail
        );
        return;
    }

    consecutiveInconclusiveChecks = 0;

    if (!lastSnapshotProbe.resolutionAttempted) {
        consecutiveMediaServiceFailures = 0;
    }

    bool independentMediaFailureThisCheck = false;
    MediaCanaryOutcome mediaCanaryOutcomeThisCheck =
        MediaCanaryOutcome::Inconclusive;

    if (lastSnapshotProbe.conclusive && controlAvailable) {
        mediaCanaryOutcomeThisCheck = runIndependentMediaCanary();

        if (
            mediaCanaryOutcomeThisCheck ==
                MediaCanaryOutcome::MediaReceived
        ) {
            recordSuccessfulHealthCheck(
                HealthEvidenceSource::MediaCanaryRtp,
                "RTP canario retornou; confirmando estabilidade",
                "Camera validada por RTP canario",
                "Camera recuperada e RTP canario validado"
            );
            return;
        }

        independentMediaFailureThisCheck =
            mediaCanaryOutcomeThisCheck ==
                MediaCanaryOutcome::AllProfilesSilentAfterPlay;

        if (independentMediaFailureThisCheck) {
            if (consecutiveIndependentMediaFailures < 255) {
                consecutiveIndependentMediaFailures++;
            }
        } else if (
            mediaCanaryOutcomeThisCheck == MediaCanaryOutcome::Inconclusive
        ) {
            consecutiveIndependentMediaFailures = 0;
        }
    } else {
        consecutiveIndependentMediaFailures = 0;
    }

    consecutiveSuccesses = 0;
    failedHealthChecks++;

    if (lastSnapshotProbe.conclusive && consecutiveFailures < 255) {
        if (
            mediaServiceTransportFailure &&
            consecutiveMediaServiceFailures > consecutiveFailures
        ) {
            consecutiveFailures = consecutiveMediaServiceFailures;
        } else {
            consecutiveFailures++;
        }

    }

    if (controlAvailable) {
        consecutiveControlFailures = 0;

        if (millis() - bootAtMs < STARTUP_GRACE_MS) {
            setState(
                CameraState::Degraded,
                String("Camera em tolerancia inicial; imagem falhou: ") +
                    lastSnapshotProbe.detail
            );
            return;
        }

        Serial.printf(
            "SNAPSHOT FALHOU: tentativas=%u | IP=%s | HTTP=%d | %s\n",
            consecutiveFailures,
            camera.valid ? camera.ip.toString().c_str() : "desconhecido",
            lastSnapshotProbe.statusCode,
            lastSnapshotProbe.detail.c_str()
        );

        if (
            consecutiveFailures >= VIDEO_FAILURES_BEFORE_ALERT &&
            mediaCanaryOutcomeThisCheck ==
                MediaCanaryOutcome::AllProfilesSilentAfterPlay
        ) {
            beginOrEscalateIncident(IncidentKind::SnapshotUnavailable);
            queueIncidentAlert();
        }

        if (consecutiveFailures >= VIDEO_FAILURES_BEFORE_REBOOT) {
            liveStreamRelay.snapshotStats(relayStats);
            const bool liveMediaActive =
                liveRelayHasRecentMedia(relayStats);
            const bool rebootEligible =
                lastOnvifProbe.protocolResponded &&
                snapshotFailureAllowsAutomaticReboot() &&
                !liveMediaActive &&
                independentMediaFailureThisCheck &&
                consecutiveIndependentMediaFailures >=
                    VIDEO_FAILURES_BEFORE_REBOOT;

            if (rebootEligible && requestCameraRebootOnvif()) {
                return;
            }

            if (ENABLE_ONVIF_AUTO_REBOOT && !rebootEligible) {
                lastCameraRebootResult = currentTimestamp() +
                    " - nao solicitado: ";

                if (!lastOnvifProbe.protocolResponded) {
                    lastCameraRebootResult +=
                        "servico ONVIF sem resposta para receber SystemReboot";
                } else if (liveMediaActive) {
                    lastCameraRebootResult +=
                        "fluxo RTP recente; reboot adiado";
                } else if (!independentMediaFailureThisCheck) {
                    lastCameraRebootResult +=
                        "sonda RTP independente inconclusiva; reboot bloqueado";
                } else if (
                    consecutiveIndependentMediaFailures <
                        VIDEO_FAILURES_BEFORE_REBOOT
                ) {
                    lastCameraRebootResult +=
                        "falha RTSP independente ainda nao confirmada";
                } else if (snapshotProfileFailoverPending) {
                    lastCameraRebootResult +=
                        "perfil de snapshot alternativo ainda em teste";
                } else {
                    lastCameraRebootResult +=
                        "a falha indica autenticacao, recurso nao suportado ou configuracao";
                }
            }
        }

        setState(
            CameraState::Degraded,
            String("Camera responde, mas o snapshot falhou: ") +
                lastSnapshotProbe.detail
        );
        return;
    }

    if (consecutiveControlFailures < 255) {
        consecutiveControlFailures++;
    }

    Serial.printf(
        "CAMERA SEM CONTROLE E SEM IMAGEM: video=%u controle=%u | IP=%s\n",
        consecutiveFailures,
        consecutiveControlFailures,
        camera.valid ? camera.ip.toString().c_str() : "desconhecido"
    );

    if (
        consecutiveControlFailures < FAILURES_BEFORE_OFFLINE ||
        millis() - bootAtMs < STARTUP_GRACE_MS
    ) {
        setState(
            CameraState::Degraded,
            "Camera nao respondeu; confirmando indisponibilidade"
        );
        return;
    }

    beginOrEscalateIncident(IncidentKind::CameraOffline);
    queueIncidentAlert();
    setState(
        CameraState::Offline,
        "Camera confirmada como indisponivel e sem imagem"
    );
}

void appendOnvifEndpointJson(
    String& json,
    const char* name,
    const OnvifServiceEndpoint& endpoint,
    bool prependComma
)
{
    if (prependComma) {
        json += ',';
    }

    json += '"';
    json += name;
    json += "\":{\"available\":";
    json += endpoint.available ? "true" : "false";
    json += ",\"url\":\"";
    json += jsonEscape(endpoint.url);
    json += "\",\"version\":\"";
    json += endpoint.majorVersion;
    json += '.';
    json += endpoint.minorVersion;
    json += "\"}";
}

String buildStatusJson()
{
    refreshCameraRebootWindow();
    const uint32_t heapTotalBytes = ESP.getHeapSize();
    const uint32_t heapFreeBytes = ESP.getFreeHeap();
    const uint32_t heapMinimumFreeBytes = ESP.getMinFreeHeap();
    const uint32_t heapLargestBlockBytes = ESP.getMaxAllocHeap();
    const uint32_t psramTotalBytes = ESP.getPsramSize();
    const uint32_t psramFreeBytes = ESP.getFreePsram();
    const uint32_t psramMinimumFreeBytes = ESP.getMinFreePsram();
    const uint32_t psramLargestBlockBytes = ESP.getMaxAllocPsram();
    const unsigned long now = millis();

    String json;
    json.reserve(14000);

    json += "{";
    json += "\"firmwareVersion\":\"";
    json += FIRMWARE_VERSION;
    json += "\",\"state\":\"";
    json += stateName(cameraState);
    json += "\",\"lastEvent\":\"";
    json += jsonEscape(lastEvent);
    json += "\",\"uptimeSeconds\":";
    json += now / 1000UL;
    json += ",\"stateSinceSeconds\":";
    json += (now - lastStateChangeAtMs) / 1000UL;
    json += ',';

    json += "\"resources\":{";
    json += "\"heap\":{";
    json += "\"totalBytes\":";
    json += heapTotalBytes;
    json += ",\"freeBytes\":";
    json += heapFreeBytes;
    json += ",\"minimumFreeBytes\":";
    json += heapMinimumFreeBytes;
    json += ",\"largestBlockBytes\":";
    json += heapLargestBlockBytes;
    json += "},\"psram\":{";
    json += "\"totalBytes\":";
    json += psramTotalBytes;
    json += ",\"freeBytes\":";
    json += psramFreeBytes;
    json += ",\"minimumFreeBytes\":";
    json += psramMinimumFreeBytes;
    json += ",\"largestBlockBytes\":";
    json += psramLargestBlockBytes;
    json += "},\"flash\":{";
    json += "\"chipBytes\":";
    json += ESP.getFlashChipSize();
    json += ",\"sketchBytes\":";
    json += ESP.getSketchSize();
    json += ",\"freeSketchBytes\":";
    {
        const esp_partition_t *runningPartition = esp_ota_get_running_partition();
        const uint32_t partitionBytes = runningPartition ? runningPartition->size : 0;
        const uint32_t sketchBytes = ESP.getSketchSize();
        json += (partitionBytes > sketchBytes) ? (partitionBytes - sketchBytes) : 0;
    }
    json += "},\"chip\":{";
    json += "\"model\":\"";
    json += ESP.getChipModel();
    json += "\",\"revision\":";
    json += static_cast<unsigned int>(ESP.getChipRevision());
    json += ",\"cores\":";
    json += static_cast<unsigned int>(ESP.getChipCores());
    json += ",\"cpuFrequencyMHz\":";
    json += ESP.getCpuFreqMHz();
    const esp_reset_reason_t resetReason = esp_reset_reason();
    json += ",\"resetReason\":\"";
    json += resetReasonName(resetReason);
    json += "\",\"resetReasonCode\":";
    json += static_cast<int>(resetReason);
    json += "}},";

    json += "\"bluetoothMemory\":{";
    json += "\"configured\":";
    json += RELEASE_BLUETOOTH_MEMORY ? "true" : "false";
    json += ",\"attempted\":";
    json += bluetoothMemoryReleaseAttempted ? "true" : "false";
    json += ",\"result\":";
    json += static_cast<int>(bluetoothMemoryReleaseResult);
    json += ",\"controllerStatusBefore\":";
    json += static_cast<int>(bluetoothControllerStatusBeforeRelease);
    json += ",\"startedBefore\":";
    json += bluetoothStartedBeforeRelease ? "true" : "false";
    json += ",\"heapBeforeBytes\":";
    json += heapBeforeBluetoothReleaseBytes;
    json += ",\"heapAfterBytes\":";
    json += heapAfterBluetoothReleaseBytes;
    json += ",\"releasedBytes\":";
    json += bluetoothMemoryReleasedBytes;
    json += "},";

    json += "\"wifi\":{";
    json += "\"connected\":";
    json += WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"ssid\":\"";
    json += jsonEscape(WiFi.SSID());
    json += "\",\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\",\"gateway\":\"";
    json += WiFi.gatewayIP().toString();
    json += "\",\"rssi\":";
    json += WiFi.RSSI();
    json += "},";

    json += "\"alertLed\":{\"configured\":";
    json += ENABLE_CAMERA_ALERT_LED ? "true" : "false";
    json += ",\"blinking\":";
    json += cameraAlertLedBlinking ? "true" : "false";
    json += ",\"on\":";
    json += cameraAlertLedOn ? "true" : "false";
    json += ",\"reason\":\"";

    if (!ENABLE_CAMERA_ALERT_LED) {
        json += "disabled";
    } else if (WiFi.status() != WL_CONNECTED) {
        json += "wifi_disconnected";
    } else if (activeIncident != IncidentKind::None) {
        json += incidentName(activeIncident);
    } else if (cameraState == CameraState::Offline) {
        json += "camera_offline";
    } else if (cameraState == CameraState::Recovering) {
        json += "camera_recovering";
    } else {
        json += "none";
    }

    json += "\"},";

    json += "\"camera\":{";
    json += "\"name\":\"";
    json += jsonEscape(CAMERA_NAME);
    json += "\",\"known\":";
    json += camera.valid ? "true" : "false";
    json += ",\"ip\":\"";
    json += camera.valid ? camera.ip.toString() : String("");
    json += "\",\"onvifUrl\":\"";
    json += jsonEscape(camera.onvifUrl);
    json += "\",\"uuid\":\"";
    json += jsonEscape(camera.endpointUuid);
    json += "\",\"scopes\":\"";
    json += jsonEscape(camera.scopes);
    json += "\",\"rtspPort\":";
    json += camera.rtspPort;
    json += "},";

    json += "\"onvif\":{";
    json += "\"catalogResolved\":";
    json += onvifServices.resolved ? "true" : "false";
    json += ",\"usedMediaFallback\":";
    json += onvifServices.usedMediaFallback ? "true" : "false";
    json += ",\"serviceCount\":";
    json += onvifServices.serviceCount;
    json += ",\"attempts\":";
    json += onvifServices.attempts;
    json += ",\"successes\":";
    json += onvifServices.successes;
    json += ",\"failures\":";
    json += onvifServices.failures;
    json += ",\"rejectedEndpoints\":";
    json += onvifServices.rejectedEndpoints;
    json += ",\"lastStatus\":";
    json += onvifServices.lastStatusCode;
    json += ",\"lastLatencyMs\":";
    json += onvifServices.lastLatencyMs;
    json += ",\"ageSeconds\":";
    json += onvifServices.resolvedAtMs == 0
        ? 0UL
        : (now - onvifServices.resolvedAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(onvifServices.lastResult);
    json += "\",\"authentication\":{";
    json += "\"usernameToken\":";
    json += onvifServices.usernameToken ? "true" : "false";
    json += ",\"httpDigest\":";
    json += onvifServices.httpDigest ? "true" : "false";
    json += "},\"capabilities\":{";
    json += "\"snapshotUri\":";
    json += onvifServices.mediaSnapshotUri ? "true" : "false";
    json += ",\"rtpRtspTcp\":";
    json += onvifServices.mediaRtpRtspTcp ? "true" : "false";
    json += ",\"media2RtspStreaming\":";
    json += onvifServices.media2RtspStreaming ? "true" : "false";
    json += ",\"eventsPullPoint\":";
    json += onvifServices.eventsPullPoint ? "true" : "false";
    json += ",\"ptzStatus\":";
    json += onvifServices.ptzStatus ? "true" : "false";
    json += ",\"imagingStabilizationAdvertised\":";
    json += onvifServices.imagingStabilizationAdvertised ? "true" : "false";
    json += ",\"analyticsRules\":";
    json += onvifServices.analyticsRules ? "true" : "false";
    json += ",\"analyticsModules\":";
    json += onvifServices.analyticsModules ? "true" : "false";
    json += "},\"services\":{";
    appendOnvifEndpointJson(json, "device", onvifServices.device, false);
    appendOnvifEndpointJson(json, "media1", onvifServices.media1, true);
    appendOnvifEndpointJson(json, "media2", onvifServices.media2, true);
    appendOnvifEndpointJson(json, "events", onvifServices.events, true);
    appendOnvifEndpointJson(json, "ptz", onvifServices.ptz, true);
    appendOnvifEndpointJson(json, "imaging", onvifServices.imaging, true);
    appendOnvifEndpointJson(json, "analytics", onvifServices.analytics, true);
    appendOnvifEndpointJson(json, "deviceIo", onvifServices.deviceIo, true);
    json += "},\"deviceInfo\":{";
    json += "\"loaded\":";
    json += onvifMetadata.loaded ? "true" : "false";
    json += ",\"inventoryComplete\":";
    json += onvifMetadata.loadedAtMs != 0 ? "true" : "false";
    json += ",\"manufacturer\":\"";
    json += jsonEscape(onvifMetadata.manufacturer);
    json += "\",\"model\":\"";
    json += jsonEscape(onvifMetadata.model);
    json += "\",\"firmware\":\"";
    json += jsonEscape(onvifMetadata.firmware);
    json += "\",\"dateTimeType\":\"";
    json += jsonEscape(onvifMetadata.dateTimeType);
    json += "\",\"reportedTimeZone\":\"";
    json += jsonEscape(onvifMetadata.timeZone);
    json += "\",\"scopeCount\":";
    json += onvifMetadata.scopeCount;
    json += ",\"profileStreaming\":";
    json += onvifMetadata.profileStreaming ? "true" : "false";
    json += ",\"profileT\":";
    json += onvifMetadata.profileT ? "true" : "false";
    json += ",\"requests\":";
    json += onvifMetadata.requests;
    json += ",\"failures\":";
    json += onvifMetadata.failures;
    json += ",\"lastResult\":\"";
    json += jsonEscape(onvifMetadata.lastResult);
    json += "\"},\"events\":{";
    json += "\"propertiesLoaded\":";
    json += onvifMetadata.eventPropertiesLoaded ? "true" : "false";
    json += ",\"topicCount\":";
    json += onvifMetadata.eventTopicCount;
    json += ",\"descriptionCount\":";
    json += onvifMetadata.eventDescriptionCount;
    json += ",\"motion\":";
    json += onvifMetadata.motionEvents ? "true" : "false";
    json += ",\"tamper\":";
    json += onvifMetadata.tamperEvents ? "true" : "false";
    json += ",\"signalLoss\":";
    json += onvifMetadata.signalLossEvents ? "true" : "false";
    json += ",\"imageQuality\":";
    json += onvifMetadata.imageQualityEvents ? "true" : "false";
    json += "}},";

    json += "\"probes\":{";
    json += "\"onvifTcp\":";
    json += lastOnvifProbe.tcpConnected ? "true" : "false";
    json += ",\"onvifHttp\":";
    json += lastOnvifProbe.protocolResponded ? "true" : "false";
    json += ",\"onvifStatus\":";
    json += lastOnvifProbe.statusCode;
    json += ",\"onvifLatencyMs\":";
    json += lastOnvifProbe.latencyMs;
    json += ",\"rtspTcp\":";
    json += lastRtspProbe.tcpConnected ? "true" : "false";
    json += ",\"rtspProtocol\":";
    json += lastRtspProbe.protocolResponded ? "true" : "false";
    json += ",\"rtspMediaRouteTested\":";
    json += lastRtspProbe.mediaRouteTested ? "true" : "false";
    json += ",\"rtspStatus\":";
    json += lastRtspProbe.statusCode;
    json += ",\"rtspLatencyMs\":";
    json += lastRtspProbe.latencyMs;
    json += "},";

    json += "\"mediaCanary\":{";
    json += "\"cycles\":";
    json += mediaCanaryCycles;
    json += ",\"runs\":";
    json += mediaCanaryRuns;
    json += ",\"successes\":";
    json += mediaCanarySuccesses;
    json += ",\"failures\":";
    json += mediaCanaryFailures;
    json += ",\"inconclusive\":";
    json += mediaCanaryInconclusive;
    json += ",\"sweepPending\":";
    json += mediaCanarySweepPending;
    json += ",\"attempted\":";
    json += lastMediaCanaryProbe.attempted ? "true" : "false";
    json += ",\"descriptorResolved\":";
    json += lastMediaCanaryProbe.descriptorResolved ? "true" : "false";
    json += ",\"rtspResponded\":";
    json += lastMediaCanaryProbe.rtspResponded ? "true" : "false";
    json += ",\"playAccepted\":";
    json += lastMediaCanaryProbe.playAccepted ? "true" : "false";
    json += ",\"mediaReceived\":";
    json += lastMediaCanaryProbe.mediaReceived ? "true" : "false";
    json += ",\"status\":";
    json += lastMediaCanaryProbe.statusCode;
    json += ",\"latencyMs\":";
    json += lastMediaCanaryProbe.latencyMs;
    json += ",\"observedAfterPlayMs\":";
    json += lastMediaCanaryProbe.observedAfterPlayMs;
    json += ",\"bytes\":";
    json += lastMediaCanaryProbe.bytes;
    json += ",\"profileIndex\":";
    json += lastMediaCanaryProfileIndex;
    json += ",\"profilesTested\":";
    json += lastMediaCanaryProfilesTested;
    json += ",\"silentProfilesMask\":";
    json += mediaCanarySilentProfilesMask;
    json += ",\"ageSeconds\":";
    json += lastMediaCanaryAtMs == 0
        ? 0UL
        : (now - lastMediaCanaryAtMs) / 1000UL;
    json += ",\"detail\":\"";
    json += jsonEscape(lastMediaCanaryProbe.detail);
    json += "\"},";

    json += "\"healthEvidence\":{\"source\":\"";
    json += healthEvidenceSourceName(lastHealthEvidenceSource);
    json += "\",\"ageSeconds\":";
    json += lastHealthEvidenceAtMs == 0
        ? 0UL
        : (now - lastHealthEvidenceAtMs) / 1000UL;
    json += "},";

    json += "\"snapshot\":{";
    json += "\"ready\":";
    json += snapshotTarget.valid ? "true" : "false";
    json += ",\"attempted\":";
    json += lastSnapshotProbe.attempted ? "true" : "false";
    json += ",\"requestAttempted\":";
    json += lastSnapshotProbe.requestAttempted ? "true" : "false";
    json += ",\"resolutionAttempted\":";
    json += lastSnapshotProbe.resolutionAttempted ? "true" : "false";
    json += ",\"resolutionTransportFailure\":";
    json += lastSnapshotProbe.resolutionTransportFailure ? "true" : "false";
    json += ",\"conclusive\":";
    json += lastSnapshotProbe.conclusive ? "true" : "false";
    json += ",\"profileFailoverPending\":";
    json += snapshotProfileFailoverPending ? "true" : "false";
    json += ",\"targetResolved\":";
    json += lastSnapshotProbe.targetResolved ? "true" : "false";
    json += ",\"healthy\":";
    json += lastSnapshotProbe.jpegValid ? "true" : "false";
    json += ",\"httpResponded\":";
    json += lastSnapshotProbe.httpResponded ? "true" : "false";
    json += ",\"status\":";
    json += lastSnapshotProbe.statusCode;
    json += ",\"latencyMs\":";
    json += lastSnapshotProbe.latencyMs;
    json += ",\"bytesRead\":";
    json += static_cast<unsigned long>(lastSnapshotProbe.bytesRead);
    json += ",\"profile\":\"";
    json += jsonEscape(snapshotTarget.profileName);
    json += "\",\"detail\":\"";
    json += jsonEscape(lastSnapshotProbe.detail);
    json += "\",\"served\":";
    json += snapshotsServed;
    json += ",\"errors\":";
    json += snapshotErrors;
    json += ",\"cameraHttpRequests\":";
    json += cameraJpegHttpRequests;
    json += ",\"previewFetchOperations\":";
    json += previewCameraFetches;
    json += ",\"busyResponses\":";
    json += previewBusyResponses;
    json += ",\"clientAborts\":";
    json += previewClientAborts;
    json += ",\"bytesRelayed\":";
    appendLiveUnsigned64(json, previewBytesRelayed);
    json += ",\"healthEvidenceUses\":";
    json += previewHealthEvidenceUses;
    json += ",\"lastDeliveredAgeSeconds\":";
    json += lastBrowserSnapshotDeliveredAtMs == 0
        ? 0UL
        : (now - lastBrowserSnapshotDeliveredAtMs) / 1000UL;
    json += ",\"lastDeliveredBytes\":";
    json += lastBrowserSnapshotBytes;
    json += ",\"lastDeliveredLatencyMs\":";
    json += lastBrowserSnapshotLatencyMs;
    json += ",\"probeRuns\":";
    json += snapshotProbeRuns;
    json += ",\"probeFailures\":";
    json += snapshotProbeFailures;
    json += ",\"lastResult\":\"";
    json += jsonEscape(lastSnapshotResult);
    json += "\"},";

    appendLiveStatusJson(json);

    json += "\"incident\":{";
    json += "\"kind\":\"";
    json += incidentName(activeIncident);
    json += "\",\"active\":";
    json += activeIncident != IncidentKind::None ? "true" : "false";
    json += ",\"alertSent\":";
    json += incidentAlertSent ? "true" : "false";
    json += ",\"pendingAlert\":";
    json += pendingIncidentEmail ? "true" : "false";
    json += ",\"durationSeconds\":";
    json += incidentStartedAtMs == 0
        ? 0UL
        : (now - incidentStartedAtMs) / 1000UL;
    json += "},";

    json += "\"recovery\":{";
    json += "\"autoRebootEnabled\":";
    json += ENABLE_ONVIF_AUTO_REBOOT ? "true" : "false";
    json += ",\"credentialsConfigured\":";
    json += cameraCredentialsConfigured() ? "true" : "false";
    json += ",\"graceActive\":";
    json += cameraRebootGraceActive() ? "true" : "false";
    json += ",\"graceRemainingSeconds\":";
    json += cameraRebootGraceRemainingMs() / 1000UL;
    json += ",\"attempts\":";
    json += cameraRebootAttempts;
    json += ",\"accepted\":";
    json += cameraRebootAccepted;
    json += ",\"errors\":";
    json += cameraRebootErrors;
    json += ",\"attemptsInWindow\":";
    json += cameraRebootAttemptsInWindow;
    json += ",\"maxAttemptsInWindow\":";
    json += MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW;
    json += ",\"lastAttemptAgeSeconds\":";
    json += lastCameraRebootAttemptAtMs == 0
        ? 0UL
        : (now - lastCameraRebootAttemptAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(lastCameraRebootResult);
    json += "\"},";

    json += "\"counters\":{";
    json += "\"totalChecks\":";
    json += totalHealthChecks;
    json += ",\"successfulChecks\":";
    json += successfulHealthChecks;
    json += ",\"failedChecks\":";
    json += failedHealthChecks;
    json += ",\"inconclusiveChecks\":";
    json += inconclusiveHealthChecks;
    json += ",\"consecutiveFailures\":";
    json += consecutiveFailures;
    json += ",\"consecutiveImageFailures\":";
    json += consecutiveFailures;
    json += ",\"consecutiveControlFailures\":";
    json += consecutiveControlFailures;
    json += ",\"consecutiveInconclusiveChecks\":";
    json += consecutiveInconclusiveChecks;
    json += ",\"consecutiveMediaServiceFailures\":";
    json += consecutiveMediaServiceFailures;
    json += ",\"consecutiveIndependentMediaFailures\":";
    json += consecutiveIndependentMediaFailures;
    json += ",\"consecutiveSuccesses\":";
    json += consecutiveSuccesses;
    json += ",\"discoveryRuns\":";
    json += discoveryRuns;
    json += ",\"subnetScanRuns\":";
    json += subnetScanRuns;
    json += ",\"emailsSent\":";
    json += emailsSent;
    json += ",\"emailErrors\":";
    json += emailErrors;
    json += "},";

    json += "\"lastDiscovery\":\"";
    json += jsonEscape(lastDiscoveryResult);
    json += "\",\"lastSubnetScan\":\"";
    json += jsonEscape(lastScanResult);
    json += "\",\"lastEmailResult\":\"";
    json += jsonEscape(lastEmailResult);
    json += "\",\"resendConfigured\":";
    json += resendConfigured() ? "true" : "false";
    json += ",\"ui\":{";
    json += "\"feedMinimumIntervalMs\":";
    json += FEED_MIN_INTERVAL_MS;
    json += ",\"feedTargetCycleMs\":";
    json += FEED_TARGET_CYCLE_MS;
    json += ",\"previewMaximumFps\":";
    json += String(1000.0f / FEED_MIN_INTERVAL_MS, 2);
    json += ",\"statusRefreshIntervalMs\":";
    json += STATUS_REFRESH_INTERVAL_MS;
    json += ",\"snapshotHttpTimeoutMs\":";
    json += SNAPSHOT_HTTP_TIMEOUT_MS;
    json += "},\"settings\":{";
    json += "\"defaultsRevision\":";
    json += RUNTIME_SETTINGS_DEFAULTS_REVISION;
    json += ",\"revision\":";
    json += settingsRevision;
    json += ",\"source\":\"";
    json += settingsLoadedFromNvs ? "nvs" : "factory";
    json += "\",\"lastResult\":\"";
    json += jsonEscape(lastSettingsResult);
    json += "\"},\"adminAction\":{";
    json += "\"pending\":\"";
    json += adminActionName(pendingAdminAction);
    json += "\",\"lastResult\":\"";
    json += jsonEscape(lastAdminActionResult);
    json += "\"}}";

    return json;
}

void sendJsonResponse(int statusCode, const String& json)
{
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(
        statusCode,
        "application/json; charset=utf-8",
        json
    );
}

void sendApiError(
    int statusCode,
    const char* code,
    const String& detail
)
{
    String json;
    json.reserve(detail.length() + 96);
    json += "{\"ok\":false,\"error\":\"";
    json += code;
    json += "\",\"detail\":\"";
    json += jsonEscape(detail);
    json += "\"}";
    sendJsonResponse(statusCode, json);
}

bool authorizeSettingsMutation(String& error)
{
    if (healthCheckInProgress) {
        error = "Verificacao de saude em andamento; tente novamente";
        return false;
    }

    if (!httpServer.hasArg("writeToken")) {
        error = "Token de gravacao ausente; recarregue a aba Configuracoes";
        return false;
    }

    if (httpServer.arg("writeToken") != settingsWriteToken) {
        error = "Token de gravacao invalido; recarregue a pagina";
        return false;
    }

    if (
        adminPasswordConfigured() &&
        (
            !httpServer.hasArg("adminPassword") ||
            !secureTextEquals(
                httpServer.arg("adminPassword"),
                DASHBOARD_ADMIN_PASSWORD_VALUE
            )
        )
    ) {
        error = "Senha administrativa incorreta";
        return false;
    }

    return true;
}

bool readStringSettingArgument(
    const char* name,
    size_t minimumLength,
    size_t maximumLength,
    String& value,
    String& error
)
{
    if (!httpServer.hasArg(name)) {
        error = String("Campo ausente: ") + name;
        return false;
    }

    value = httpServer.arg(name);
    value.trim();

    if (value.length() < minimumLength || value.length() > maximumLength) {
        error = String("Campo fora do tamanho permitido: ") + name;
        return false;
    }

    for (size_t index = 0; index < value.length(); index++) {
        const uint8_t byte = static_cast<uint8_t>(value.charAt(index));

        if (byte < 0x20 || byte == 0x7F) {
            error = String("Campo contem caractere invalido: ") + name;
            return false;
        }
    }

    return true;
}

bool readUnsignedSettingArgument(
    const char* name,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t& value,
    String& error
)
{
    if (!httpServer.hasArg(name)) {
        error = String("Campo ausente: ") + name;
        return false;
    }

    String textValue = httpServer.arg(name);
    textValue.trim();

    if (textValue.isEmpty()) {
        error = String("Campo vazio: ") + name;
        return false;
    }

    uint64_t parsed = 0;

    for (size_t index = 0; index < textValue.length(); index++) {
        const char character = textValue.charAt(index);

        if (character < '0' || character > '9') {
            error = String("Campo numerico invalido: ") + name;
            return false;
        }

        parsed = parsed * 10ULL + static_cast<uint8_t>(character - '0');

        if (parsed > maximum) {
            error = String("Campo acima do limite: ") + name;
            return false;
        }
    }

    if (parsed < minimum) {
        error = String("Campo abaixo do limite: ") + name;
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

bool readBooleanSettingArgument(
    const char* name,
    uint8_t& value,
    String& error
)
{
    if (!httpServer.hasArg(name)) {
        error = String("Campo ausente: ") + name;
        return false;
    }

    String textValue = httpServer.arg(name);
    textValue.toLowerCase();

    if (textValue == "1" || textValue == "true" || textValue == "on") {
        value = 1;
        return true;
    }

    if (textValue == "0" || textValue == "false" || textValue == "off") {
        value = 0;
        return true;
    }

    error = String("Opcao booleana invalida: ") + name;
    return false;
}

bool parseRuntimeSettingsRequest(
    RuntimeSettings& candidate,
    String& error
)
{
    String textValue;
    uint32_t numericValue = 0;

    if (!readStringSettingArgument(
        "cameraName",
        CAMERA_NAME_MIN_LENGTH,
        CAMERA_NAME_MAX_LENGTH,
        textValue,
        error
    )) {
        return false;
    }
    copySettingText(candidate.cameraName, textValue);

    if (!readStringSettingArgument(
        "snapshotProfileMatch",
        0,
        SNAPSHOT_PROFILE_MATCH_MAX_LENGTH,
        textValue,
        error
    )) {
        return false;
    }
    copySettingText(candidate.snapshotProfileMatch, textValue);

#define READ_U32(field, argument, minimum, maximum) \
    if (!readUnsignedSettingArgument( \
        argument, minimum, maximum, numericValue, error \
    )) { \
        return false; \
    } \
    candidate.field = numericValue

#define READ_U16(field, argument, minimum, maximum) \
    READ_U32(field, argument, minimum, maximum); \
    candidate.field = static_cast<uint16_t>(numericValue)

#define READ_U8(field, argument, minimum, maximum) \
    READ_U32(field, argument, minimum, maximum); \
    candidate.field = static_cast<uint8_t>(numericValue)

#define READ_BOOL(field, argument) \
    if (!readBooleanSettingArgument(argument, candidate.field, error)) { \
        return false; \
    }

    READ_U16(
        cameraRtspPort,
        "cameraRtspPort",
        CAMERA_RTSP_PORT_MIN,
        CAMERA_RTSP_PORT_MAX
    );
    READ_U32(
        healthCheckIntervalMs,
        "healthCheckIntervalMs",
        HEALTH_CHECK_INTERVAL_MIN_MS,
        HEALTH_CHECK_INTERVAL_MAX_MS
    );
    READ_U32(
        startupGraceMs,
        "startupGraceMs",
        STARTUP_GRACE_MIN_MS,
        STARTUP_GRACE_MAX_MS
    );
    READ_U8(
        failuresBeforeRediscovery,
        "failuresBeforeRediscovery",
        FAILURES_BEFORE_REDISCOVERY_MIN,
        FAILURES_BEFORE_REDISCOVERY_MAX
    );
    READ_U8(
        failuresBeforeOffline,
        "failuresBeforeOffline",
        FAILURES_BEFORE_OFFLINE_MIN,
        FAILURES_BEFORE_OFFLINE_MAX
    );
    READ_U8(
        successesBeforeRecovery,
        "successesBeforeRecovery",
        SUCCESSES_BEFORE_RECOVERY_MIN,
        SUCCESSES_BEFORE_RECOVERY_MAX
    );
    READ_U8(
        videoFailuresBeforeAlert,
        "videoFailuresBeforeAlert",
        VIDEO_FAILURES_BEFORE_ALERT_MIN,
        VIDEO_FAILURES_BEFORE_ALERT_MAX
    );
    READ_U8(
        videoFailuresBeforeReboot,
        "videoFailuresBeforeReboot",
        VIDEO_FAILURES_BEFORE_REBOOT_MIN,
        VIDEO_FAILURES_BEFORE_REBOOT_MAX
    );

    READ_BOOL(enableOnvifAutoReboot, "enableOnvifAutoReboot");
    READ_U32(
        onvifRebootGraceMs,
        "onvifRebootGraceMs",
        ONVIF_REBOOT_GRACE_MIN_MS,
        ONVIF_REBOOT_GRACE_MAX_MS
    );
    READ_U32(
        onvifRebootCooldownMs,
        "onvifRebootCooldownMs",
        ONVIF_REBOOT_COOLDOWN_MIN_MS,
        ONVIF_REBOOT_COOLDOWN_MAX_MS
    );
    READ_U32(
        onvifRebootWindowMs,
        "onvifRebootWindowMs",
        ONVIF_REBOOT_WINDOW_MIN_MS,
        ONVIF_REBOOT_WINDOW_MAX_MS
    );
    READ_U8(
        maxOnvifRebootAttemptsPerWindow,
        "maxOnvifRebootAttemptsPerWindow",
        MAX_ONVIF_REBOOT_ATTEMPTS_MIN,
        MAX_ONVIF_REBOOT_ATTEMPTS_MAX
    );

    READ_U32(
        wifiConnectTimeoutMs,
        "wifiConnectTimeoutMs",
        WIFI_CONNECT_TIMEOUT_MIN_MS,
        WIFI_CONNECT_TIMEOUT_MAX_MS
    );
    READ_BOOL(enableAutomaticSubnetScan, "enableAutomaticSubnetScan");
    READ_U32(
        discoveryTimeoutMs,
        "discoveryTimeoutMs",
        DISCOVERY_TIMEOUT_MIN_MS,
        DISCOVERY_TIMEOUT_MAX_MS
    );
    READ_U32(
        rediscoveryIntervalMs,
        "rediscoveryIntervalMs",
        REDISCOVERY_INTERVAL_MIN_MS,
        REDISCOVERY_INTERVAL_MAX_MS
    );
    READ_U32(
        subnetScanIntervalMs,
        "subnetScanIntervalMs",
        SUBNET_SCAN_INTERVAL_MIN_MS,
        SUBNET_SCAN_INTERVAL_MAX_MS
    );

    READ_BOOL(enableEmailAlerts, "enableEmailAlerts");
    READ_BOOL(emailOnIpChange, "emailOnIpChange");
    READ_U32(
        emailRetryIntervalMs,
        "emailRetryIntervalMs",
        EMAIL_RETRY_INTERVAL_MIN_MS,
        EMAIL_RETRY_INTERVAL_MAX_MS
    );

    READ_U32(
        tcpConnectTimeoutMs,
        "tcpConnectTimeoutMs",
        TCP_CONNECT_TIMEOUT_MIN_MS,
        TCP_CONNECT_TIMEOUT_MAX_MS
    );
    READ_U32(
        serviceResponseTimeoutMs,
        "serviceResponseTimeoutMs",
        SERVICE_RESPONSE_TIMEOUT_MIN_MS,
        SERVICE_RESPONSE_TIMEOUT_MAX_MS
    );
    READ_U32(
        scanConnectTimeoutMs,
        "scanConnectTimeoutMs",
        SCAN_CONNECT_TIMEOUT_MIN_MS,
        SCAN_CONNECT_TIMEOUT_MAX_MS
    );
    READ_U32(
        snapshotHttpTimeoutMs,
        "snapshotHttpTimeoutMs",
        SNAPSHOT_HTTP_TIMEOUT_MIN_MS,
        SNAPSHOT_HTTP_TIMEOUT_MAX_MS
    );
    READ_U32(
        snapshotResolveRetryIntervalMs,
        "snapshotResolveRetryIntervalMs",
        SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS,
        SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS
    );
    READ_U8(
        snapshotFailuresBeforeReresolve,
        "snapshotFailuresBeforeReresolve",
        SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN,
        SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX
    );

    READ_BOOL(enableCameraAlertLed, "enableCameraAlertLed");
    READ_U32(
        cameraAlertLedBlinkIntervalMs,
        "cameraAlertLedBlinkIntervalMs",
        CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS,
        CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS
    );
    READ_U32(
        feedMinIntervalMs,
        "feedMinIntervalMs",
        FEED_MIN_INTERVAL_MIN_MS,
        FEED_MIN_INTERVAL_MAX_MS
    );
    READ_U32(
        feedTargetCycleMs,
        "feedTargetCycleMs",
        FEED_TARGET_CYCLE_MIN_MS,
        FEED_TARGET_CYCLE_MAX_MS
    );
    READ_U32(
        statusRefreshIntervalMs,
        "statusRefreshIntervalMs",
        STATUS_REFRESH_INTERVAL_MIN_MS,
        STATUS_REFRESH_INTERVAL_MAX_MS
    );

#undef READ_BOOL
#undef READ_U8
#undef READ_U16
#undef READ_U32

    return validateRuntimeSettings(candidate, error);
}

String buildSettingsJson()
{
    String json;
    json.reserve(3400);
    json += "{\"ok\":true,\"schemaVersion\":";
    json += RUNTIME_SETTINGS_SCHEMA_VERSION;
    json += ",\"defaultsRevision\":";
    json += RUNTIME_SETTINGS_DEFAULTS_REVISION;
    json += ",\"source\":\"";
    json += settingsLoadedFromNvs ? "nvs" : "factory";
    json += "\",\"revision\":";
    json += settingsRevision;
    json += ",\"writeCount\":";
    json += settingsWriteCount;
    json += ",\"lastResult\":\"";
    json += jsonEscape(lastSettingsResult);
    json += "\",\"security\":{";
    json += "\"passwordRequired\":";
    json += adminPasswordConfigured() ? "true" : "false";
    json += ",\"writeToken\":\"";
    json += settingsWriteToken;
    json += "\"},\"estimates\":{";
    json += "\"imageAlertSeconds\":";
    json += (
        static_cast<uint64_t>(HEALTH_CHECK_INTERVAL_MS) *
        VIDEO_FAILURES_BEFORE_ALERT
    ) / 1000ULL;
    json += ",\"imageRebootSeconds\":";
    json += (
        static_cast<uint64_t>(HEALTH_CHECK_INTERVAL_MS) *
        VIDEO_FAILURES_BEFORE_REBOOT
    ) / 1000ULL;
    json += ",\"offlineSeconds\":";
    json += (
        static_cast<uint64_t>(HEALTH_CHECK_INTERVAL_MS) *
        FAILURES_BEFORE_OFFLINE
    ) / 1000ULL;
    json += "},\"values\":{";
    json += "\"cameraName\":\"";
    json += jsonEscape(CAMERA_NAME);
    json += "\",\"snapshotProfileMatch\":\"";
    json += jsonEscape(CAMERA_SNAPSHOT_PROFILE_MATCH);
    json += "\",\"cameraRtspPort\":";
    json += CAMERA_RTSP_PORT;
    json += ",\"healthCheckIntervalMs\":";
    json += HEALTH_CHECK_INTERVAL_MS;
    json += ",\"startupGraceMs\":";
    json += STARTUP_GRACE_MS;
    json += ",\"failuresBeforeRediscovery\":";
    json += FAILURES_BEFORE_REDISCOVERY;
    json += ",\"failuresBeforeOffline\":";
    json += FAILURES_BEFORE_OFFLINE;
    json += ",\"successesBeforeRecovery\":";
    json += SUCCESSES_BEFORE_RECOVERY;
    json += ",\"videoFailuresBeforeAlert\":";
    json += VIDEO_FAILURES_BEFORE_ALERT;
    json += ",\"videoFailuresBeforeReboot\":";
    json += VIDEO_FAILURES_BEFORE_REBOOT;
    json += ",\"enableOnvifAutoReboot\":";
    json += ENABLE_ONVIF_AUTO_REBOOT ? "true" : "false";
    json += ",\"onvifRebootGraceMs\":";
    json += ONVIF_REBOOT_GRACE_MS;
    json += ",\"onvifRebootCooldownMs\":";
    json += ONVIF_REBOOT_COOLDOWN_MS;
    json += ",\"onvifRebootWindowMs\":";
    json += ONVIF_REBOOT_WINDOW_MS;
    json += ",\"maxOnvifRebootAttemptsPerWindow\":";
    json += MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW;
    json += ",\"wifiConnectTimeoutMs\":";
    json += WIFI_CONNECT_TIMEOUT_MS;
    json += ",\"enableAutomaticSubnetScan\":";
    json += ENABLE_AUTOMATIC_SUBNET_SCAN ? "true" : "false";
    json += ",\"discoveryTimeoutMs\":";
    json += DISCOVERY_TIMEOUT_MS;
    json += ",\"rediscoveryIntervalMs\":";
    json += REDISCOVERY_INTERVAL_MS;
    json += ",\"subnetScanIntervalMs\":";
    json += SUBNET_SCAN_INTERVAL_MS;
    json += ",\"enableEmailAlerts\":";
    json += ENABLE_EMAIL_ALERTS ? "true" : "false";
    json += ",\"emailOnIpChange\":";
    json += EMAIL_ON_IP_CHANGE ? "true" : "false";
    json += ",\"emailRetryIntervalMs\":";
    json += EMAIL_RETRY_INTERVAL_MS;
    json += ",\"tcpConnectTimeoutMs\":";
    json += TCP_CONNECT_TIMEOUT_MS;
    json += ",\"serviceResponseTimeoutMs\":";
    json += SERVICE_RESPONSE_TIMEOUT_MS;
    json += ",\"scanConnectTimeoutMs\":";
    json += SCAN_CONNECT_TIMEOUT_MS;
    json += ",\"snapshotHttpTimeoutMs\":";
    json += SNAPSHOT_HTTP_TIMEOUT_MS;
    json += ",\"snapshotResolveRetryIntervalMs\":";
    json += SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS;
    json += ",\"snapshotFailuresBeforeReresolve\":";
    json += SNAPSHOT_FAILURES_BEFORE_RERESOLVE;
    json += ",\"enableCameraAlertLed\":";
    json += ENABLE_CAMERA_ALERT_LED ? "true" : "false";
    json += ",\"cameraAlertLedBlinkIntervalMs\":";
    json += CAMERA_ALERT_LED_BLINK_INTERVAL_MS;
    json += ",\"feedMinIntervalMs\":";
    json += FEED_MIN_INTERVAL_MS;
    json += ",\"feedTargetCycleMs\":";
    json += FEED_TARGET_CYCLE_MS;
    json += ",\"statusRefreshIntervalMs\":";
    json += STATUS_REFRESH_INTERVAL_MS;
    json += "}}";
    return json;
}

void handleSaveSettings()
{
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    RuntimeSettings candidate = settings;
    String validationError;

    if (!parseRuntimeSettingsRequest(candidate, validationError)) {
        sendApiError(422, "invalid_settings", validationError);
        return;
    }

    if (!saveRuntimeSettingsToNvs(candidate)) {
        sendApiError(500, "nvs_write_failed", "Falha ao gravar a NVS");
        return;
    }

    const RuntimeSettings previous = settings;
    settings = candidate;
    settingsLoadedFromNvs = true;
    settingsRevision++;
    settingsWriteCount++;
    lastSettingsResult = currentTimestamp() + " - configuracoes salvas";
    applyRuntimeSettings(previous);
    sendJsonResponse(200, buildSettingsJson());
}

void handleResetSettings()
{
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    if (!clearRuntimeSettingsNvs()) {
        sendApiError(500, "nvs_clear_failed", "Falha ao limpar a NVS");
        return;
    }

    const RuntimeSettings previous = settings;
    settings = makeDefaultRuntimeSettings();
    settingsLoadedFromNvs = false;
    settingsRevision++;
    settingsWriteCount++;
    lastSettingsResult = currentTimestamp() + " - valores de fabrica restaurados";
    applyRuntimeSettings(previous);
    sendJsonResponse(200, buildSettingsJson());
}

bool queueAdminAction(AdminAction action, String& error)
{
    if (pendingAdminAction != AdminAction::None) {
        error = String("Outra acao esta em andamento: ") +
            adminActionName(pendingAdminAction);
        return false;
    }

    pendingAdminAction = action;
    lastAdminActionResult = currentTimestamp() + " - acao enfileirada: " +
        adminActionName(action);
    return true;
}

void handleAdminAction(AdminAction action)
{
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    String queueError;

    if (!queueAdminAction(action, queueError)) {
        sendApiError(409, "action_busy", queueError);
        return;
    }

    String json = "{\"ok\":true,\"queued\":\"";
    json += adminActionName(action);
    json += "\",\"message\":\"Acao recebida e sera executada pelo loop principal\"}";
    sendJsonResponse(202, json);
}

void configureHttpServer()
{
    httpServer.on("/", HTTP_GET, [] {
        httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        httpServer.sendHeader("Pragma", "no-cache");
        httpServer.send_P(200, "text/html; charset=utf-8", DASHBOARD_PAGE);
    });

    httpServer.on("/api/status", HTTP_GET, [] {
        httpServer.send(
            200,
            "application/json; charset=utf-8",
            buildStatusJson()
        );
    });

    httpServer.on("/assets/live-player.js", HTTP_GET, [] {
        httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        httpServer.sendHeader("Pragma", "no-cache");
        httpServer.send_P(
            200,
            "application/javascript; charset=utf-8",
            LIVE_PLAYER_SCRIPT
        );
    });

    httpServer.on("/api/camera/snapshot", HTTP_GET, [] {
        serveCameraSnapshot();
    });

    httpServer.on("/api/settings", HTTP_GET, [] {
        sendJsonResponse(200, buildSettingsJson());
    });

    httpServer.on("/api/settings", HTTP_POST, [] {
        handleSaveSettings();
    });

    httpServer.on("/api/settings/reset", HTTP_POST, [] {
        handleResetSettings();
    });

    httpServer.on("/api/actions/health-check", HTTP_POST, [] {
        handleAdminAction(AdminAction::HealthCheck);
    });

    httpServer.on("/api/actions/rediscover", HTTP_POST, [] {
        handleAdminAction(AdminAction::Rediscover);
    });

    httpServer.on("/api/actions/reboot-camera", HTTP_POST, [] {
        handleAdminAction(AdminAction::CameraReboot);
    });

    httpServer.on("/api/actions/reset-reboot-limiter", HTTP_POST, [] {
        handleAdminAction(AdminAction::ResetRebootLimiter);
    });

    httpServer.on("/api/actions/reboot-esp32", HTTP_POST, [] {
        handleAdminAction(AdminAction::RestartEsp32);
    });

    httpServer.on("/health", HTTP_GET, [] {
        httpServer.send(200, "text/plain", "ok");
    });

    httpServer.onNotFound([] {
        httpServer.send(404, "application/json", "{\"error\":\"not_found\"}");
    });

    httpServer.begin();
    Serial.printf("Painel HTTP iniciado na porta %u.\n", HTTP_SERVER_PORT);
}

bool connectToWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);

    Serial.printf("Conectando a rede \"%s\"", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long startedAt = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS
    ) {
        updateCameraAlertLed();
        Serial.print('.');
        delay(500);
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Falha ao conectar ao Wi-Fi.");
        return false;
    }

    Serial.printf("Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Mascara: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    wifiWasConnected = true;

    return true;
}

void initializeConnectedServices()
{
    if (
        connectedServicesInitialized ||
        WiFi.status() != WL_CONNECTED
    ) {
        return;
    }

    connectedServicesInitialized = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_SERVER_PORT);
        MDNS.addService("camera-live", "tcp", LIVE_STREAM_PORT);
        Serial.printf("mDNS: http://%s.local\n", HOSTNAME);
    }

    configureHttpServer();

    if (!loadCameraTarget()) {
        loadCameraIpHint();
    }

    if (!camera.valid) {
        discoverOnvifCamera();
    }

    if (camera.valid && resolveCameraOnvifEndpoint()) {
        resolveOnvifServiceCatalog();
        resolveSnapshotTarget();
    }

    configureLiveRelay();

    Serial.printf("Painel: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Painel mDNS: http://%s.local\n", HOSTNAME);
    Serial.printf(
        "Ao vivo integrado: http://%s:%u\n",
        WiFi.localIP().toString().c_str(),
        LIVE_STREAM_PORT
    );

    runHealthCheck();
}

void processPendingEmails()
{
    if (!ENABLE_EMAIL_ALERTS) {
        pendingIncidentEmail = false;
        pendingRecoveryEmail = false;
        return;
    }

    if (
        millis() - lastEmailAttemptAtMs < EMAIL_RETRY_INTERVAL_MS &&
        lastEmailAttemptAtMs != 0
    ) {
        return;
    }

    if (pendingIncidentEmail) {
        sendIncidentAlert();
        return;
    }

    if (pendingRecoveryEmail) {
        sendRecoveryAlert();
    }
}

void processPendingAdminAction()
{
    if (pendingAdminAction == AdminAction::None) {
        return;
    }

    const AdminAction action = pendingAdminAction;
    pendingAdminAction = AdminAction::None;

    switch (action) {
        case AdminAction::HealthCheck:
            lastAdminActionResult =
                currentTimestamp() + " - verificacao manual iniciada";
            runHealthCheck();
            lastAdminActionResult =
                currentTimestamp() + " - verificacao manual concluida: " +
                lastEvent;
            break;

        case AdminAction::Rediscover: {
            CameraIoGuard cameraIoGuard;

            if (!cameraIoGuard.acquired()) {
                lastAdminActionResult = currentTimestamp() +
                    " - redescoberta nao executada: relay ao vivo em uso";
                break;
            }

            setState(
                CameraState::Discovering,
                "Redescoberta manual solicitada pelo painel"
            );
            const bool found = discoverOnvifCamera();

            if (found) {
                resolveCameraOnvifEndpoint();
                invalidateSnapshotTarget();
                invalidateOnvifServiceCatalog();
                resetSnapshotProfileFailover();
                lastSnapshotResolveAttemptAtMs = 0;
                resolveOnvifServiceCatalog();
                runHealthCheck();
            }

            lastAdminActionResult = currentTimestamp() +
                (found
                    ? " - camera redescoberta e verificada"
                    : " - redescoberta concluida sem camera compativel");
            break;
        }

        case AdminAction::CameraReboot: {
            const bool accepted = requestCameraRebootOnvif(true);
            lastAdminActionResult = currentTimestamp() +
                (accepted
                    ? " - reboot ONVIF manual aceito"
                    : " - reboot ONVIF manual nao executado: ") +
                lastCameraRebootResult;
            break;
        }

        case AdminAction::ResetRebootLimiter:
            lastCameraRebootAttemptAtMs = 0;
            cameraRebootWindowStartedAtMs = 0;
            cameraRebootAttemptsInWindow = 0;

            for (
                uint8_t index = 0;
                index < MAX_ONVIF_REBOOT_ATTEMPTS_MAX;
                index++
            ) {
                cameraRebootAcceptedAtMs[index] = 0;
            }

            lastCameraRebootResult =
                currentTimestamp() + " - limitador de reboots reiniciado";
            lastAdminActionResult = lastCameraRebootResult;
            break;

        case AdminAction::RestartEsp32:
            espRestartRequestedAtMs = millis();
            lastAdminActionResult =
                currentTimestamp() + " - reinicializacao do ESP32 agendada";
            break;

        case AdminAction::None:
            break;
    }
}

void maintainWiFi()
{
    static unsigned long lastReconnectAttemptAtMs = 0;

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiWasConnected) {
            wifiWasConnected = true;
            resetCameraCheckSequences(
                "Wi-Fi restabelecido; aguardando nova verificacao"
            );
            lastHealthCheckAtMs = 0;
            Serial.printf(
                "Wi-Fi restabelecido. IP: %s\n",
                WiFi.localIP().toString().c_str()
            );
        }

        return;
    }

    if (wifiWasConnected) {
        wifiWasConnected = false;
        resetCameraCheckSequences("ESP32 perdeu a conexao Wi-Fi");
        setState(CameraState::Degraded, "ESP32 perdeu a conexao Wi-Fi");
    }

    if (millis() - lastReconnectAttemptAtMs < 10000UL) {
        return;
    }

    lastReconnectAttemptAtMs = millis();
    Serial.println("Tentando reconectar o ESP32 ao Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

}

void setup()
{
    Serial.begin(115200);
    settingsWriteToken = makeSettingsWriteToken();
    loadRuntimeSettings();
    configureCameraAlertLed();
    updateCameraAlertLed();
    delay(800);

    bootAtMs = millis();
    lastStateChangeAtMs = bootAtMs;

    Serial.println();
    Serial.println("ESP32 Camera Watchdog");
    Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
    Serial.printf("Chip: %s rev. %u\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("Flash: %u MB\n", ESP.getFlashChipSize() / 1024U / 1024U);
    releaseUnusedBluetoothMemory();
    Serial.printf("Heap inicial disponivel: %u bytes\n", ESP.getFreeHeap());

    if (!connectToWiFi()) {
        setState(CameraState::Degraded, "ESP32 sem Wi-Fi");
    }

    initializeConnectedServices();
}

void loop()
{
    updateCameraAlertLed();
    maintainWiFi();
    initializeConnectedServices();

    if (connectedServicesInitialized) {
        httpServer.handleClient();
        refreshOnvifDeviceMetadata();
    }
    processPendingEmails();
    processPendingAdminAction();

    if (
        espRestartRequestedAtMs != 0 &&
        millis() - espRestartRequestedAtMs >= ADMIN_RESTART_DELAY_MS
    ) {
        Serial.println("Reiniciando o ESP32 por solicitacao administrativa...");
        delay(50);
        ESP.restart();
    }

    if (millis() - lastHealthCheckAtMs >= HEALTH_CHECK_INTERVAL_MS) {
        runHealthCheck();
    }

    updateCameraAlertLed();
    delay(2);
}
