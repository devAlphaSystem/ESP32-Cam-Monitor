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
#include <nvs.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "live_stream.h"
#include "wifi_setup.h"
#if RELEASE_BLUETOOTH_MEMORY
#include <esp32-hal-bt.h>
#endif
#include "persistent_log.h"
#include "runtime_settings.h"
#include "secrets.h"

namespace CameraWatchdog {

extern const char FIRMWARE_VERSION[];
extern const char DASHBOARD_PAGE[] PROGMEM;

constexpr size_t ONVIF_PTZ_PRESET_CAPACITY = 128;
constexpr size_t CAMERA_OPERATION_HEAP_RESERVE = 70000;
constexpr size_t CAMERA_ALTERNATE_ENDPOINT_CAPACITY = 4;
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
    RebootAllCameras,
    ResetRebootLimiter,
    RestartEsp32
};

enum class OnvifMetadataStage {
    DeviceInformation,
    IdentityReference,
    NetworkInterfaces,
    Scopes,
    SystemTime,
    EventProperties,
    Complete
};

enum class HealthEvidenceSource {
    None,
    SnapshotJpeg,
    PreviewJpeg,
    LiveStream
};

extern GlobalSettings globalSettings;

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
    String serialNumber;
    String manufacturer;
    String model;
    String interfaceMacs;
    uint32_t discoveryGroup = 0;
    String scopes;
};

struct OnvifServiceEndpoint {
    bool available = false;
    bool validated = false;
    String url;
    uint16_t majorVersion = 0;
    uint16_t minorVersion = 0;
};

struct OnvifServiceCatalog {
    bool resolved = false;
    bool catalogValid = false;
    bool stale = false;
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
    uint16_t rejectedAuthenticationEndpoints = 0;
    uint16_t inaccessibleEndpoints = 0;
    uint16_t unvalidatedEndpoints = 0;
    uint16_t invalidResponseEndpoints = 0;
    uint32_t lastLatencyMs = 0;
    unsigned long resolvedAtMs = 0;
    int lastStatusCode = 0;
    String lastResult = "Not queried yet";
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
    String lastResult = "Not queried yet";
    String lastCommandToken;
    String lastCommandResult = "No movement requested";
    std::unique_ptr<OnvifPtzPreset[]> presets;
};

struct OnvifDeviceMetadata {
    bool loaded = false;
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
    String lastResult = "Not queried yet";
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
    String lastResult = "Not checked yet";
};

struct SntpReferenceSnapshot {
    bool synchronized = false;
    unsigned long synchronizedAtMs = 0;
    uint32_t generation = 0;
};

struct ServiceProbe {
    bool tcpConnected = false;
    bool protocolResponded = false;
    bool soapResponse = false;
    int statusCode = 0;
    uint32_t latencyMs = 0;
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
    String detail = "Not run yet";
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

struct CameraContext {
    uint32_t id = 0;
    RuntimeSettings settings = makeDefaultRuntimeSettings();
    unsigned long registeredAtMs = 0;
    unsigned long lastPreviewRequestAtMs = 0;
    uint32_t lastDiscoveryGeneration = 0;
    bool settingsLoadedFromNvs = false;
    bool pendingIpChangeEmail = false;
    std::unique_ptr<CameraContext> next;
    CameraSnapshotHttpSession cameraSnapshotHttpSession = {};
    CameraTarget camera = {};
    String alternateOnvifUrls[CAMERA_ALTERNATE_ENDPOINT_CAPACITY];
    uint8_t alternateOnvifCount = 0;
    uint8_t preferredFailureStreak = 0;
    unsigned long lastAlternateAttemptAtMs = 0;
    uint8_t nextAlternateIndex = 0;
    unsigned long lastIdentityVerificationAtMs = 0;
    OnvifServiceCatalog onvifServices = {};
    OnvifPtzStatus onvifPtz = {};
    OnvifDeviceMetadata onvifMetadata = {};
    CameraClockStatus cameraClockStatus = {};
    OnvifMetadataStage onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
    SnapshotTarget snapshotTarget = {};
    SnapshotProbe lastSnapshotProbe = {};
    ServiceProbe lastOnvifProbe = {};
    ServiceProbe lastRtspProbe = {};
    bool lastRtspProbeAttempted = false;
    CameraState cameraState = CameraState::Starting;
    IncidentKind activeIncident = IncidentKind::None;
    IncidentKind recoveryIncident = IncidentKind::None;
    unsigned long lastHealthCheckAtMs = 0;
    unsigned long lastEmailAttemptAtMs = 0;
    unsigned long lastStateChangeAtMs = 0;
    unsigned long incidentStartedAtMs = 0;
    unsigned long recoveryIncidentDurationMs = 0;
    unsigned long lastCameraRebootAttemptAtMs = 0;
    unsigned long cameraRebootGraceStartedAtMs = 0;
    unsigned long cameraRebootWindowStartedAtMs = 0;
    uint32_t totalHealthChecks = 0;
    uint32_t successfulHealthChecks = 0;
    uint32_t failedHealthChecks = 0;
    uint32_t inconclusiveHealthChecks = 0;
    uint32_t emailsSent = 0;
    uint32_t emailErrors = 0;
    uint32_t snapshotsServed = 0;
    uint32_t snapshotErrors = 0;
    uint32_t previewCameraFetches = 0;
    uint32_t previewBusyResponses = 0;
    uint64_t previewBytesTransferred = 0;
    uint32_t cameraJpegHttpRequests = 0;
    uint32_t cameraJpegConnectionReuses = 0;
    uint32_t cameraJpegDigestPreemptiveRequests = 0;
    uint32_t snapshotProbeRuns = 0;
    uint32_t snapshotProbeFailures = 0;
    uint32_t previewClientAborts = 0;
    uint32_t previewHealthEvidenceUses = 0;
    uint32_t cameraRebootAttempts = 0;
    uint32_t cameraRebootAccepted = 0;
    uint32_t cameraRebootErrors = 0;
    uint32_t automaticCameraRebootAttempts = 0;
    uint32_t automaticCameraRebootAccepted = 0;
    uint32_t automaticCameraRebootErrors = 0;
    uint32_t manualCameraRebootAttempts = 0;
    uint32_t manualCameraRebootAccepted = 0;
    uint32_t manualCameraRebootErrors = 0;
    uint8_t consecutiveFailures = 0;
    uint8_t consecutiveSuccesses = 0;
    uint8_t consecutiveControlFailures = 0;
    uint8_t consecutiveInconclusiveChecks = 0;
    uint8_t consecutiveMediaServiceFailures = 0;
    uint8_t snapshotTargetFailureStreak = 0;
    uint8_t cameraRebootAttemptsInWindow = 0;
    uint8_t onvifMetadataStageFailureStreak = 0;
    unsigned long cameraRebootAcceptedAtMs[MAX_ONVIF_REBOOT_ATTEMPTS_MAX] = {0};
    CameraRebootEvidence cameraRebootHistory[CAMERA_REBOOT_HISTORY_CAPACITY] = {};
    size_t cameraRebootHistoryCount = 0;
    size_t cameraRebootHistoryNext = 0;
    bool incidentAlertSent = false;
    bool pendingIncidentEmail = false;
    bool pendingRecoveryEmail = false;
    String lastEvent = "Starting up";
    String lastEmailResult = "No email sent";
    String previousCameraIp = {};
    String lastSnapshotResult = "Not requested yet";
    String lastCameraRebootResult = "Not requested yet";
    String lastCameraRebootEvidence = "No reboot accepted in this boot";
    String snapshotProfileTokenToSkip = {};
    unsigned long lastSnapshotResolveAttemptAtMs = 0;
    unsigned long lastBrowserSnapshotDeliveredAtMs = 0;
    unsigned long lastHealthEvidenceAtMs = 0;
    unsigned long lastOnvifMetadataRequestAtMs = 0;
    uint32_t lastBrowserSnapshotBytes = 0;
    uint32_t lastBrowserSnapshotLatencyMs = 0;
    int lastSnapshotResolveStatusCode = 0;
    bool lastSnapshotResolveTransportFailure = false;
    bool snapshotProfileFailoverPending = false;
    bool lastOnvifMetadataRequestFailed = false;
    HealthEvidenceSource lastHealthEvidenceSource = HealthEvidenceSource::None;
};

extern WebServer httpServer;
extern WiFiUDP discoveryUdp;
extern SemaphoreHandle_t cameraIoMutex;
extern uint16_t cameraIoDepth;

bool ensureCameraIoMutex();
class CameraIoGuard {
public:
    explicit CameraIoGuard(TickType_t timeout = 0) : acquired_(ensureCameraIoMutex() && xSemaphoreTakeRecursive(cameraIoMutex, timeout) == pdTRUE)
    {
        if (acquired_)
            cameraIoDepth++;
    }

    ~CameraIoGuard()
    {
        if (acquired_) {
            cameraIoDepth--;
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
extern unsigned long factoryResetRequestedAtMs;
extern bool factoryResetFailed;

extern char discoveryBuffer[WS_DISCOVERY_BUFFER_SIZE + 1];
extern size_t discoveredCount;

extern unsigned long lastRediscoveryAtMs;
extern unsigned long lastSubnetScanAtMs;
extern unsigned long lastCameraAlertLedToggleAtMs;

extern uint32_t discoveryRuns;
extern uint32_t subnetScanRuns;
extern uint16_t nextLogContextId;
extern uint32_t heapBeforeBluetoothReleaseBytes;
extern uint32_t heapAfterBluetoothReleaseBytes;
extern uint32_t bluetoothMemoryReleasedBytes;

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
extern esp_err_t bluetoothMemoryReleaseResult;
extern esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease;

extern String lastDiscoveryResult;
extern String lastScanResult;

bool validateRuntimeSettings(const RuntimeSettings& values, String& error);
bool saveRuntimeSettingsToNvs(CameraContext& ctx, const RuntimeSettings& values);
bool saveGlobalSettingsToNvs(const GlobalSettings& values);
bool validateGlobalSettings(const GlobalSettings& values, String& error);
void loadRuntimeSettings();
String makeSettingsWriteToken();
bool adminPasswordConfigured();
bool secureTextEquals(const String& supplied, const char* expected);
const char* adminActionName(AdminAction action);
AdminAction currentAdminAction();
const char* stateName(CameraState state);
const char* resetReasonName(esp_reset_reason_t reason);
const char* incidentName(IncidentKind kind);
const char* healthEvidenceSourceName(HealthEvidenceSource source);
void beginOrEscalateIncident(CameraContext& ctx, IncidentKind kind);
void queueIncidentAlert(CameraContext& ctx);
void resolveActiveIncident(CameraContext& ctx);
void configureCameraAlertLed();
void updateCameraAlertLed();
void releaseUnusedBluetoothMemory();
void appendJsonEscaped(String& output, const String& input);
String jsonEscape(const String& input);
void appendUnsigned64(String& output, uint64_t value);
String htmlEscape(const String& input);
String currentTimestamp();
bool cameraCredentialsConfigured(CameraContext& ctx);
void noteSntpSynchronization(struct timeval* value);
bool clockSynchronized();
SntpReferenceSnapshot snapshotSntpReference();
bool sntpReferenceFresh(const SntpReferenceSnapshot& snapshot);
String makeWsseSecurityHeader(CameraContext& ctx);
bool cameraRebootGraceActive(CameraContext& ctx);
unsigned long cameraRebootGraceRemainingMs(CameraContext& ctx);
void refreshCameraRebootWindow(CameraContext& ctx);
String durationText(unsigned long durationMs);
void applyRuntimeSettings(CameraContext& ctx, const RuntimeSettings& previous);
void resetCameraCheckSequences(CameraContext& ctx, const char* snapshotDetail);
void setState(CameraContext& ctx, CameraState newState, const String& event);
bool resendConfigured();
bool readHttpResponseBodyBounded(HTTPClient& request, String& output, size_t limit, bool& limitExceeded, bool* streamFailure = nullptr);
bool sendIncidentAlert(CameraContext& ctx);
bool sendRecoveryAlert(CameraContext& ctx);
bool sendIpChangeAlert(CameraContext& ctx, const String& oldIp, const String& newIp);
uint16_t allocateLogContextId();

bool parseHttpUrl(const String& url, ParsedUrl& parsed);
IPAddress uint32ToIp(uint32_t value);
void maintainCameraSnapshotHttpSession(CameraContext& ctx);
void resetCameraSnapshotHttpSession(CameraContext& ctx);
void invalidateSnapshotTarget(CameraContext& ctx);
void resetSnapshotProfileFailover(CameraContext& ctx);
bool discoverOnvifCameras(bool manualRequest = false, uint16_t contextId = 0);
void maintainDiscoveryIdentityCandidates();
ServiceProbe probeOnvif(CameraContext& ctx, const CameraTarget& target, uint16_t contextId = 0);
bool resolveCameraOnvifEndpoint(CameraContext& ctx, uint16_t contextId = 0);
bool probeCameraIdentity(CameraContext& ctx, CameraTarget& target, uint16_t contextId = 0, bool* identityConflict = nullptr);
void maintainCameraClock(CameraContext& ctx);
void invalidateOnvifServiceCatalog(CameraContext& ctx);
String limitedOnvifText(String value, size_t maximumLength = 120);
bool resolveOnvifServiceCatalog(CameraContext& ctx, uint16_t contextId = 0);
void refreshOnvifDeviceMetadata(CameraContext& ctx);
bool resolveSnapshotTarget(CameraContext& ctx, uint16_t contextId = 0);
int onvifPtzPresetIndex(CameraContext& ctx, const String& token);
bool onvifPtzPresetIsDaemon(const String& token);
bool requestOnvifPtzPreset(CameraContext& ctx, const String& presetToken, uint16_t contextId = 0);
void maintainOnvifPtzPresets(CameraContext& ctx);
bool probeCameraSnapshotJpeg(CameraContext& ctx, uint16_t contextId = 0);
bool resolveLiveStreamTarget(CameraContext& ctx, bool highQuality, LiveStreamTarget& target, const char*& error, uint16_t contextId = 0);
bool snapshotFailureAllowsAutomaticReboot(CameraContext& ctx);
bool requestCameraRebootOnvif(CameraContext& ctx, bool manualRequest = false, uint16_t contextId = 0);
void serveCameraSnapshot(CameraContext& ctx);
ServiceProbe probeRtsp(CameraContext& ctx, const IPAddress& ip, uint16_t port, uint16_t contextId = 0);
void maintainSubnetScan();

extern std::unique_ptr<CameraContext> cameras;
extern size_t cameraCount;
extern uint32_t registryRevision;
extern String lastRegistryResult;
extern bool registryBusy;
extern bool discoveryRequested;
extern bool subnetScanRequested;
extern uint32_t pendingAdminCameraId;
extern uint32_t activeAdminCameraId;
extern uint32_t batchNextCameraId;
extern uint32_t batchLastCameraId;
extern uint32_t batchCompleted;
extern uint32_t batchAccepted;
extern uint32_t batchFailed;
CameraContext* findCamera(uint32_t id);
CameraContext* registerCameraTarget(const CameraTarget& target, String& error);
bool mergeCameraRecords(uint32_t primaryId, uint32_t secondaryId, bool confirmSettings, String& error);
bool forgetCameraRecord(uint32_t id, String& error);
bool reconcileIdentifiedCameras();
bool cameraIdWasMerged(uint32_t id, uint32_t* primaryId = nullptr);
bool cameraIdentityMatches(const CameraTarget& left, const CameraTarget& right);
String normalizeCameraIdentifier(String value);
bool switchToAlternateEndpoint(CameraContext& ctx);
bool persistCameraMergeAlias(uint32_t secondaryId, uint32_t primaryId);
bool persistCamera(CameraContext& ctx);
bool suspendCameraForIdentityConflict(CameraContext& ctx, const char* reason);
bool persistRebootLimiter(CameraContext& ctx);
void loadRebootLimiter(CameraContext& ctx);
void applyGlobalSettings(const GlobalSettings& previous);
bool cameraHealthCheckDue(const CameraContext& ctx);
bool anyCameraHealthCheckDue();
void runHealthCheck(CameraContext& ctx, bool manualRequest = false, uint16_t contextId = 0);
void configureHttpServer();
void startHttpServer();
void sendJsonResponse(int statusCode, const String& json);
void sendApiError(int statusCode, const char* code, const String& detail);
bool authorizeDashboardMutation(String& error, bool blockDuringHealthCheck);
extern bool httpServerRunning;
bool startMdns();

}
