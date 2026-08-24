#pragma once

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
#include <esp_bt.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <errno.h>
#include <stddef.h>
#include <memory>
#include <new>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#if RELEASE_BLUETOOTH_MEMORY
#include <esp32-hal-bt.h>
#endif
#include "persistent_log.h"
#include "runtime_settings.h"
#include "secrets.h"

#ifndef CAMERA_ONVIF_USERNAME_VALUE
#define CAMERA_ONVIF_USERNAME_VALUE ""
#endif

#ifndef CAMERA_ONVIF_PASSWORD_VALUE
#define CAMERA_ONVIF_PASSWORD_VALUE ""
#endif

#ifndef DASHBOARD_ADMIN_PASSWORD_VALUE
#define DASHBOARD_ADMIN_PASSWORD_VALUE ""
#endif

namespace CameraWatchdog {

extern const char FIRMWARE_VERSION[];
extern const char DASHBOARD_PAGE[] PROGMEM;

constexpr size_t ONVIF_PTZ_PRESET_CAPACITY = 128;
constexpr int32_t CAMERA_CLOCK_HEALTHY_DRIFT_MS = 5000L;

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
    GotoPtzPreset,
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

enum class HealthEvidenceSource {
    None,
    SnapshotJpeg,
    PreviewJpeg
};

extern RuntimeSettings settings;

#define CAMERA_NAME (settings.cameraName)
#define LOCAL_HOSTNAME (settings.localHostname)
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
    bool eventsPullPoint = false;
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

struct OnvifPtzPreset {
    String token;
    String name;
};

struct OnvifPtzStatus {
    bool profileResolved = false;
    bool presetsLoaded = false;
    bool truncated = false;
    uint16_t presetCount = 0;
    uint16_t reportedPresetCount = 0;
    uint32_t refreshAttempts = 0;
    uint32_t refreshSuccesses = 0;
    uint32_t refreshFailures = 0;
    uint32_t commandAttempts = 0;
    uint32_t commandAccepted = 0;
    uint32_t commandErrors = 0;
    uint32_t lastRefreshLatencyMs = 0;
    uint32_t lastCommandLatencyMs = 0;
    unsigned long refreshedAtMs = 0;
    unsigned long lastRefreshAttemptAtMs = 0;
    unsigned long lastCommandAtMs = 0;
    int lastRefreshStatusCode = 0;
    int lastCommandStatusCode = 0;
    String profileToken;
    String profileName;
    String lastResult = "Ainda nao consultado";
    String lastCommandToken;
    String lastCommandResult = "Nenhum movimento solicitado";
    OnvifPtzPreset presets[ONVIF_PTZ_PRESET_CAPACITY];
};

struct OnvifDeviceMetadata {
    bool loaded = false;
    bool scopesLoaded = false;
    bool timeLoaded = false;
    bool eventPropertiesLoaded = false;
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

struct CameraClockStatus {
    bool checked = false;
    bool utcValid = false;
    bool daylightSavings = false;
    int32_t driftMs = 0;
    uint32_t roundTripMs = 0;
    uint32_t checks = 0;
    uint32_t failures = 0;
    unsigned long lastCheckAtMs = 0;
    String dateTimeType;
    String timeZone;
    String lastResult = "Ainda nao verificado";
};

struct SntpReferenceSnapshot {
    bool synchronized = false;
    unsigned long synchronizedAtMs = 0;
    uint32_t generation = 0;
};

struct ServiceProbe {
    bool tcpConnected = false;
    bool protocolResponded = false;
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

constexpr size_t CAMERA_REBOOT_HISTORY_CAPACITY = 8;

struct CameraRebootEvidence {
    char timestamp[25] = {0};
    char snapshotProfile[SNAPSHOT_PROFILE_MATCH_MAX_LENGTH + 1] = {0};
    uint32_t uptimeSeconds = 0;
    uint32_t snapshotLatencyMs = 0;
    uint32_t snapshotBytes = 0;
    uint32_t freeHeapBytes = 0;
    int16_t snapshotStatus = 0;
    int16_t esp32WifiRssi = 0;
    uint8_t consecutiveImageFailures = 0;
    bool manual = false;
};

struct DigestChallenge {
    bool valid = false;
    String realm;
    String nonce;
    String qop;
    String opaque;
    String algorithm = "MD5";
};

struct CameraSnapshotHttpSession {
    WiFiClient client;
    HTTPClient request;
    String url;
    DigestChallenge digestChallenge;
    uint32_t digestNonceCount = 0;
    unsigned long reusableAtMs = 0;
    bool forceHttp10 = false;
};

extern WebServer httpServer;
extern WiFiUDP discoveryUdp;
extern Preferences preferences;
extern CameraSnapshotHttpSession cameraSnapshotHttpSession;
extern SemaphoreHandle_t cameraIoMutex;

bool ensureCameraIoMutex();
class CameraIoGuard {
public:
    explicit CameraIoGuard(TickType_t timeout = 0) : acquired_(ensureCameraIoMutex() && xSemaphoreTakeRecursive(cameraIoMutex, timeout) == pdTRUE)
    {}

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

extern bool settingsLoadedFromNvs;
extern uint32_t settingsRevision;
extern uint32_t settingsWriteCount;
extern String settingsWriteToken;
extern String lastSettingsResult;
extern AdminAction pendingAdminAction;
extern AdminAction activeAdminAction;
extern uint16_t pendingAdminActionContextId;
extern String lastAdminActionResult;
extern String pendingPtzPresetToken;
extern unsigned long espRestartRequestedAtMs;

extern CameraTarget camera;
extern OnvifServiceCatalog onvifServices;
extern OnvifPtzStatus onvifPtz;
extern OnvifDeviceMetadata onvifMetadata;
extern CameraClockStatus cameraClockStatus;
extern OnvifMetadataStage onvifMetadataStage;
extern SnapshotTarget snapshotTarget;
extern SnapshotProbe lastSnapshotProbe;
extern ServiceProbe lastOnvifProbe;
extern ServiceProbe lastRtspProbe;
extern bool lastRtspProbeAttempted;
extern CameraState cameraState;
extern IncidentKind activeIncident;
extern IncidentKind recoveryIncident;

extern char discoveryBuffer[WS_DISCOVERY_BUFFER_SIZE + 1];
extern DiscoveryCandidate discovered[MAX_DISCOVERED_CAMERAS];
extern size_t discoveredCount;

extern unsigned long bootAtMs;
extern unsigned long lastHealthCheckAtMs;
extern unsigned long lastRediscoveryAtMs;
extern unsigned long lastSubnetScanAtMs;
extern unsigned long lastEmailAttemptAtMs;
extern unsigned long lastStateChangeAtMs;
extern unsigned long lastCameraAlertLedToggleAtMs;
extern unsigned long incidentStartedAtMs;
extern unsigned long recoveryIncidentDurationMs;
extern unsigned long lastCameraRebootAttemptAtMs;
extern unsigned long cameraRebootGraceStartedAtMs;
extern unsigned long cameraRebootWindowStartedAtMs;

extern uint32_t totalHealthChecks;
extern uint32_t successfulHealthChecks;
extern uint32_t failedHealthChecks;
extern uint32_t inconclusiveHealthChecks;
extern uint32_t discoveryRuns;
extern uint32_t subnetScanRuns;
extern uint32_t emailsSent;
extern uint32_t emailErrors;
extern uint32_t snapshotsServed;
extern uint32_t snapshotErrors;
extern uint32_t previewCameraFetches;
extern uint32_t previewBusyResponses;
extern uint64_t previewBytesTransferred;
extern uint32_t cameraJpegHttpRequests;
extern uint32_t cameraJpegConnectionReuses;
extern uint32_t cameraJpegDigestPreemptiveRequests;
extern uint32_t snapshotProbeRuns;
extern uint32_t snapshotProbeFailures;
extern uint32_t previewClientAborts;
extern uint32_t previewHealthEvidenceUses;
extern uint32_t cameraRebootAttempts;
extern uint32_t cameraRebootAccepted;
extern uint32_t cameraRebootErrors;
extern uint32_t automaticCameraRebootAttempts;
extern uint32_t automaticCameraRebootAccepted;
extern uint32_t automaticCameraRebootErrors;
extern uint32_t manualCameraRebootAttempts;
extern uint32_t manualCameraRebootAccepted;
extern uint32_t manualCameraRebootErrors;
extern uint16_t nextLogContextId;
extern uint32_t heapBeforeBluetoothReleaseBytes;
extern uint32_t heapAfterBluetoothReleaseBytes;
extern uint32_t bluetoothMemoryReleasedBytes;
extern uint8_t consecutiveFailures;
extern uint8_t consecutiveSuccesses;
extern uint8_t consecutiveControlFailures;
extern uint8_t consecutiveInconclusiveChecks;
extern uint8_t consecutiveMediaServiceFailures;
extern uint8_t snapshotTargetFailureStreak;
extern uint8_t cameraRebootAttemptsInWindow;
extern uint8_t onvifMetadataStageFailureStreak;
extern unsigned long cameraRebootAcceptedAtMs[MAX_ONVIF_REBOOT_ATTEMPTS_MAX];
extern CameraRebootEvidence cameraRebootHistory[CAMERA_REBOOT_HISTORY_CAPACITY];
extern size_t cameraRebootHistoryCount;
extern size_t cameraRebootHistoryNext;

extern bool incidentAlertSent;
extern bool pendingIncidentEmail;
extern bool pendingRecoveryEmail;
extern bool cameraAlertLedOn;
extern bool cameraAlertLedBlinking;
extern bool bluetoothMemoryReleaseAttempted;
extern bool bluetoothStartedBeforeRelease;
extern bool connectedServicesInitialized;
extern bool mdnsAvailable;
extern bool wifiWasConnected;
extern bool healthCheckInProgress;
extern portMUX_TYPE sntpReferenceMux;
extern bool espSntpSynchronized;
extern unsigned long espSntpSynchronizedAtMs;
extern uint32_t espSntpSynchronizationGeneration;
extern uint32_t loggedSntpSynchronizationGeneration;
extern esp_err_t bluetoothMemoryReleaseResult;
extern esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease;

extern String lastEvent;
extern String lastEmailResult;
extern String lastDiscoveryResult;
extern String lastScanResult;
extern String previousCameraIp;
extern String lastSnapshotResult;
extern String lastCameraRebootResult;
extern String lastCameraRebootEvidence;
extern String snapshotProfileTokenToSkip;
extern unsigned long lastSnapshotResolveAttemptAtMs;
extern unsigned long lastBrowserSnapshotDeliveredAtMs;
extern unsigned long lastHealthEvidenceAtMs;
extern unsigned long lastOnvifMetadataRequestAtMs;
extern uint32_t lastBrowserSnapshotBytes;
extern uint32_t lastBrowserSnapshotLatencyMs;
extern int lastSnapshotResolveStatusCode;
extern bool lastSnapshotResolveTransportFailure;
extern bool snapshotProfileFailoverPending;
extern bool lastOnvifMetadataRequestFailed;
extern HealthEvidenceSource lastHealthEvidenceSource;

bool validateRuntimeSettings(const RuntimeSettings& values, String& error);
bool saveRuntimeSettingsToNvs(const RuntimeSettings& values);
bool clearRuntimeSettingsNvs();
void loadRuntimeSettings();
void recordRuntimeSettingsContext(const char* source, uint8_t flags);
String makeSettingsWriteToken();
bool adminPasswordConfigured();
bool secureTextEquals(const String& supplied, const char* expected);
const char* adminActionName(AdminAction action);
AdminAction currentAdminAction();
const char* stateName(CameraState state);
const char* resetReasonName(esp_reset_reason_t reason);
const char* incidentName(IncidentKind kind);
const char* healthEvidenceSourceName(HealthEvidenceSource source);
void beginOrEscalateIncident(IncidentKind kind);
void queueIncidentAlert();
void resolveActiveIncident();
void configureCameraAlertLed();
void updateCameraAlertLed();
void releaseUnusedBluetoothMemory();
void appendJsonEscaped(String& output, const String& input);
String jsonEscape(const String& input);
void appendUnsigned64(String& output, uint64_t value);
String htmlEscape(const String& input);
String currentTimestamp();
bool cameraCredentialsConfigured();
void noteSntpSynchronization(struct timeval* value);
bool clockSynchronized();
SntpReferenceSnapshot snapshotSntpReference();
bool sntpReferenceFresh(const SntpReferenceSnapshot& snapshot);
String makeWsseSecurityHeader();
bool cameraRebootGraceActive();
unsigned long cameraRebootGraceRemainingMs();
void refreshCameraRebootWindow();
String durationText(unsigned long durationMs);
void applyRuntimeSettings(const RuntimeSettings& previous);
void setState(CameraState newState, const String& event);
bool resendConfigured();
bool readHttpResponseBodyBounded(HTTPClient& request, String& output, size_t limit, bool& limitExceeded, bool* streamFailure = nullptr);
bool sendIncidentAlert();
bool sendRecoveryAlert();
bool sendIpChangeAlert(const String& oldIp, const String& newIp);
uint16_t allocateLogContextId();

IPAddress uint32ToIp(uint32_t value);
void maintainCameraSnapshotHttpSession();
void resetCameraSnapshotHttpSession();
void invalidateSnapshotTarget();
void resetSnapshotProfileFailover();
void saveCameraTarget(uint16_t contextId = 0, uint8_t flags = PersistentLog::FlagScheduled);
bool loadCameraTarget();
bool loadCameraIpHint();
bool discoverOnvifCamera(bool manualRequest = false, uint16_t contextId = 0);
ServiceProbe probeOnvif(const CameraTarget& target, uint16_t contextId = 0);
bool resolveCameraOnvifEndpoint(uint16_t contextId = 0);
void maintainCameraClock();
void invalidateOnvifServiceCatalog();
String limitedOnvifText(String value, size_t maximumLength = 120);
bool resolveOnvifServiceCatalog(bool forceRefresh = false, uint16_t contextId = 0);
void refreshOnvifDeviceMetadata();
bool resolveSnapshotTarget(uint16_t contextId = 0);
int onvifPtzPresetIndex(const String& token);
bool onvifPtzPresetIsDaemon(const String& token);
bool requestOnvifPtzPreset(const String& presetToken, uint16_t contextId = 0);
void maintainOnvifPtzPresets();
bool probeCameraSnapshotJpeg(uint16_t contextId = 0);
bool snapshotFailureAllowsAutomaticReboot();
bool requestCameraRebootOnvif(bool manualRequest = false, uint16_t contextId = 0);
void serveCameraSnapshot();
ServiceProbe probeRtsp(const IPAddress& ip, uint16_t port, uint16_t contextId = 0);
bool scanSubnetForCamera(uint16_t contextId = 0);

void configureHttpServer();
bool startMdns();

}
