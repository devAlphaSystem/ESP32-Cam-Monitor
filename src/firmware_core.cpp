#include "firmware_internal.h"

namespace CameraWatchdog {

const char FIRMWARE_VERSION[] = "0.16.0";

constexpr char RESEND_API_URL[] = "https://api.resend.com/emails";
constexpr char SETTINGS_NVS_NAMESPACE[] = "camcfg";
constexpr char SETTINGS_NVS_KEY[] = "runtime";
constexpr char SETTINGS_DEFAULTS_REVISION_KEY[] = "defaults_rev";
constexpr uint32_t SETTINGS_MAGIC = 0x43573035UL;
constexpr uint16_t PREVIOUS_RUNTIME_SETTINGS_SCHEMA_VERSION = 4;
constexpr unsigned long ESP_SNTP_REFERENCE_MAX_AGE_MS = 24UL * 60UL * 60UL * 1000UL;

struct StoredSettingsBlob {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    RuntimeSettings values;
    uint32_t checksum;
};

constexpr size_t PREVIOUS_RUNTIME_SETTINGS_PAYLOAD_SIZE = offsetof(RuntimeSettings, cameraUsername);

struct PreviousStoredSettingsBlob {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    uint8_t values[PREVIOUS_RUNTIME_SETTINGS_PAYLOAD_SIZE];
    uint32_t checksum;
};

static_assert(PREVIOUS_RUNTIME_SETTINGS_PAYLOAD_SIZE == offsetof(RuntimeSettings, localHostname) + LOCAL_HOSTNAME_MAX_LENGTH + 1, "Credenciais devem permanecer apos os campos do esquema 4");

RuntimeSettings settings = makeDefaultRuntimeSettings();

WebServer httpServer(HTTP_SERVER_PORT);
WiFiUDP discoveryUdp;
Preferences preferences;
CameraSnapshotHttpSession cameraSnapshotHttpSession;
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
String lastSettingsResult = "Usando valores de fabrica";
AdminAction pendingAdminAction = AdminAction::None;
AdminAction activeAdminAction = AdminAction::None;
uint16_t pendingAdminActionContextId = 0;
String lastAdminActionResult = "Nenhuma acao administrativa executada";
String pendingPtzPresetToken;
unsigned long espRestartRequestedAtMs = 0;

CameraTarget camera;
OnvifServiceCatalog onvifServices;
OnvifPtzStatus onvifPtz;
OnvifDeviceMetadata onvifMetadata;
CameraClockStatus cameraClockStatus;
OnvifMetadataStage onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
SnapshotTarget snapshotTarget;
SnapshotProbe lastSnapshotProbe;
ServiceProbe lastOnvifProbe;
ServiceProbe lastRtspProbe;
bool lastRtspProbeAttempted = false;
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
uint16_t nextLogContextId = 1;
uint32_t heapBeforeBluetoothReleaseBytes = 0;
uint32_t heapAfterBluetoothReleaseBytes = 0;
uint32_t bluetoothMemoryReleasedBytes = 0;
uint8_t consecutiveFailures = 0;
uint8_t consecutiveSuccesses = 0;
uint8_t consecutiveControlFailures = 0;
uint8_t consecutiveInconclusiveChecks = 0;
uint8_t consecutiveMediaServiceFailures = 0;
uint8_t snapshotTargetFailureStreak = 0;
uint8_t cameraRebootAttemptsInWindow = 0;
uint8_t onvifMetadataStageFailureStreak = 0;
unsigned long cameraRebootAcceptedAtMs[MAX_ONVIF_REBOOT_ATTEMPTS_MAX] = {0};
CameraRebootEvidence cameraRebootHistory[CAMERA_REBOOT_HISTORY_CAPACITY];
size_t cameraRebootHistoryCount = 0;
size_t cameraRebootHistoryNext = 0;

bool incidentAlertSent = false;
bool pendingIncidentEmail = false;
bool pendingRecoveryEmail = false;
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
uint32_t loggedSntpSynchronizationGeneration = 0;
esp_err_t bluetoothMemoryReleaseResult = ESP_ERR_NOT_SUPPORTED;
esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease = ESP_BT_CONTROLLER_STATUS_NUM;

String lastEvent = "Inicializando";
String lastEmailResult = "Nenhum e-mail enviado";
String lastDiscoveryResult = "Ainda nao executada";
String lastScanResult = "Ainda nao executada";
String previousCameraIp;
String lastSnapshotResult = "Ainda nao solicitada";
String lastCameraRebootResult = "Ainda nao solicitado";
String lastCameraRebootEvidence = "Nenhum reboot aceito nesta inicializacao";
String snapshotProfileTokenToSkip;
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

uint32_t settingsChecksumBytes(const void* values, size_t valueSize)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(values);
    uint32_t hash = 2166136261UL;

    for (size_t index = 0; index < valueSize; index++) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }

    return hash;
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
        error = "Usuario da camera invalido; limite de 64 bytes, sem dois-pontos ou caracteres de controle";
        return false;
    }

    if (!settingTextValid(values.cameraPassword, sizeof(values.cameraPassword), 0, CAMERA_PASSWORD_MAX_LENGTH)) {
        error = "Senha da camera invalida; limite de 128 bytes, sem caracteres de controle";
        return false;
    }

    if (values.cameraUsername[0] == '\0' && values.cameraPassword[0] != '\0') {
        error = "Informe o usuario da camera ou remova as credenciais para acessar sem autenticacao";
        return false;
    }

    if (!settingTextValid(values.cameraName, sizeof(values.cameraName), CAMERA_NAME_MIN_LENGTH, CAMERA_NAME_MAX_LENGTH)) {
        error = "Nome da camera invalido";
        return false;
    }

    if (!localHostnameValid(values.localHostname, sizeof(values.localHostname))) {
        error = "Endereco local invalido; use letras, numeros e hifens";
        return false;
    }

    if (!settingTextValid(values.snapshotProfileMatch, sizeof(values.snapshotProfileMatch), 0, SNAPSHOT_PROFILE_MATCH_MAX_LENGTH)) {
        error = "Filtro do perfil ONVIF invalido";
        return false;
    }

#define CHECK_SETTING_RANGE(field, minimum, maximum, message) \
    if (!settingInRange(values.field, minimum, maximum)) { \
        error = message; \
        return false; \
    }

    CHECK_SETTING_RANGE(cameraRtspPort, CAMERA_RTSP_PORT_MIN, CAMERA_RTSP_PORT_MAX, "Porta RTSP fora do limite");
    CHECK_SETTING_RANGE(healthCheckIntervalMs, HEALTH_CHECK_INTERVAL_MIN_MS, HEALTH_CHECK_INTERVAL_MAX_MS, "Intervalo de verificacao fora do limite");
    CHECK_SETTING_RANGE(startupGraceMs, STARTUP_GRACE_MIN_MS, STARTUP_GRACE_MAX_MS, "Tolerancia inicial fora do limite");
    CHECK_SETTING_RANGE(failuresBeforeRediscovery, FAILURES_BEFORE_REDISCOVERY_MIN, FAILURES_BEFORE_REDISCOVERY_MAX, "Falhas para redescoberta fora do limite");
    CHECK_SETTING_RANGE(failuresBeforeOffline, FAILURES_BEFORE_OFFLINE_MIN, FAILURES_BEFORE_OFFLINE_MAX, "Falhas para offline fora do limite");
    CHECK_SETTING_RANGE(successesBeforeRecovery, SUCCESSES_BEFORE_RECOVERY_MIN, SUCCESSES_BEFORE_RECOVERY_MAX, "Sucessos para recuperacao fora do limite");
    CHECK_SETTING_RANGE(videoFailuresBeforeAlert, VIDEO_FAILURES_BEFORE_ALERT_MIN, VIDEO_FAILURES_BEFORE_ALERT_MAX, "Falhas de imagem para alerta fora do limite");
    CHECK_SETTING_RANGE(videoFailuresBeforeReboot, VIDEO_FAILURES_BEFORE_REBOOT_MIN, VIDEO_FAILURES_BEFORE_REBOOT_MAX, "Falhas de imagem para reboot fora do limite");
    CHECK_SETTING_RANGE(onvifRebootGraceMs, ONVIF_REBOOT_GRACE_MIN_MS, ONVIF_REBOOT_GRACE_MAX_MS, "Tolerancia apos reboot fora do limite");
    CHECK_SETTING_RANGE(onvifRebootCooldownMs, ONVIF_REBOOT_COOLDOWN_MIN_MS, ONVIF_REBOOT_COOLDOWN_MAX_MS, "Cooldown de reboot fora do limite");
    CHECK_SETTING_RANGE(onvifRebootWindowMs, ONVIF_REBOOT_WINDOW_MIN_MS, ONVIF_REBOOT_WINDOW_MAX_MS, "Janela de reboot fora do limite");
    CHECK_SETTING_RANGE(maxOnvifRebootAttemptsPerWindow, MAX_ONVIF_REBOOT_ATTEMPTS_MIN, MAX_ONVIF_REBOOT_ATTEMPTS_MAX, "Limite de reboots fora do intervalo");
    CHECK_SETTING_RANGE(wifiConnectTimeoutMs, WIFI_CONNECT_TIMEOUT_MIN_MS, WIFI_CONNECT_TIMEOUT_MAX_MS, "Timeout de Wi-Fi fora do limite");
    CHECK_SETTING_RANGE(discoveryTimeoutMs, DISCOVERY_TIMEOUT_MIN_MS, DISCOVERY_TIMEOUT_MAX_MS, "Timeout de descoberta fora do limite");
    CHECK_SETTING_RANGE(rediscoveryIntervalMs, REDISCOVERY_INTERVAL_MIN_MS, REDISCOVERY_INTERVAL_MAX_MS, "Intervalo de redescoberta fora do limite");
    CHECK_SETTING_RANGE(subnetScanIntervalMs, SUBNET_SCAN_INTERVAL_MIN_MS, SUBNET_SCAN_INTERVAL_MAX_MS, "Intervalo de varredura fora do limite");
    CHECK_SETTING_RANGE(emailRetryIntervalMs, EMAIL_RETRY_INTERVAL_MIN_MS, EMAIL_RETRY_INTERVAL_MAX_MS, "Intervalo de e-mail fora do limite");
    CHECK_SETTING_RANGE(tcpConnectTimeoutMs, TCP_CONNECT_TIMEOUT_MIN_MS, TCP_CONNECT_TIMEOUT_MAX_MS, "Timeout TCP fora do limite");
    CHECK_SETTING_RANGE(serviceResponseTimeoutMs, SERVICE_RESPONSE_TIMEOUT_MIN_MS, SERVICE_RESPONSE_TIMEOUT_MAX_MS, "Timeout de protocolo fora do limite");
    CHECK_SETTING_RANGE(scanConnectTimeoutMs, SCAN_CONNECT_TIMEOUT_MIN_MS, SCAN_CONNECT_TIMEOUT_MAX_MS, "Timeout da varredura fora do limite");
    CHECK_SETTING_RANGE(snapshotHttpTimeoutMs, SNAPSHOT_HTTP_TIMEOUT_MIN_MS, SNAPSHOT_HTTP_TIMEOUT_MAX_MS, "Timeout do JPEG fora do limite");
    CHECK_SETTING_RANGE(snapshotResolveRetryIntervalMs, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS, "Intervalo para resolver snapshot fora do limite");
    CHECK_SETTING_RANGE(snapshotFailuresBeforeReresolve, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX, "Falhas para resolver novamente o snapshot fora do limite");
    CHECK_SETTING_RANGE(cameraAlertLedBlinkIntervalMs, CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS, CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS, "Intervalo do LED fora do limite");
    CHECK_SETTING_RANGE(feedMinIntervalMs, FEED_MIN_INTERVAL_MIN_MS, FEED_MIN_INTERVAL_MAX_MS, "Intervalo minimo da previa fora do limite");
    CHECK_SETTING_RANGE(feedTargetCycleMs, FEED_TARGET_CYCLE_MIN_MS, FEED_TARGET_CYCLE_MAX_MS, "Ciclo da previa fora do limite");
    CHECK_SETTING_RANGE(statusRefreshIntervalMs, STATUS_REFRESH_INTERVAL_MIN_MS, STATUS_REFRESH_INTERVAL_MAX_MS, "Atualizacao do painel fora do limite");
#undef CHECK_SETTING_RANGE

    if (values.enableOnvifAutoReboot > 1 || values.enableAutomaticSubnetScan > 1 || values.enableEmailAlerts > 1 || values.emailOnIpChange > 1 || values.enableCameraAlertLed > 1) {
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

bool migrateRuntimeDefaults(RuntimeSettings& values, uint16_t storedDefaultsRevision)
{
    const RuntimeSettings defaults = makeDefaultRuntimeSettings();
    bool changed = false;

    if (storedDefaultsRevision < 2) {
        if (strcmp(values.snapshotProfileMatch, "sub") == 0) {
            copySettingText(values.snapshotProfileMatch, defaults.snapshotProfileMatch);
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
        MIGRATE_LEGACY_DEFAULT(rediscoveryIntervalMs, 60000UL);
        MIGRATE_LEGACY_DEFAULT(serviceResponseTimeoutMs, 1500UL);
        MIGRATE_LEGACY_DEFAULT(snapshotHttpTimeoutMs, 7000UL);
        MIGRATE_LEGACY_DEFAULT(snapshotResolveRetryIntervalMs, 30000UL);
        MIGRATE_LEGACY_DEFAULT(feedMinIntervalMs, 1000UL);
        MIGRATE_LEGACY_DEFAULT(feedTargetCycleMs, 1000UL);
        MIGRATE_LEGACY_DEFAULT(feedMinIntervalMs, 2000UL);
        MIGRATE_LEGACY_DEFAULT(feedTargetCycleMs, 2000UL);

#undef MIGRATE_LEGACY_DEFAULT
    }

    if (storedDefaultsRevision < 3 && values.maxOnvifRebootAttemptsPerWindow == 12) {
        values.maxOnvifRebootAttemptsPerWindow = defaults.maxOnvifRebootAttemptsPerWindow;
        changed = true;
    }

    if (storedDefaultsRevision < 4 && values.scanConnectTimeoutMs == 90UL) {
        values.scanConnectTimeoutMs = defaults.scanConnectTimeoutMs;
        changed = true;
    }

    if (storedDefaultsRevision < 5 && values.scanConnectTimeoutMs == 300UL) {
        values.scanConnectTimeoutMs = defaults.scanConnectTimeoutMs;
        changed = true;
    }

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

    const size_t bytesWritten = preferences.putBytes(SETTINGS_NVS_KEY, &blob, sizeof(blob));
    const size_t revisionBytesWritten = bytesWritten == sizeof(blob) ? preferences.putUShort(SETTINGS_DEFAULTS_REVISION_KEY, RUNTIME_SETTINGS_DEFAULTS_REVISION) : 0;
    preferences.end();
    return bytesWritten == sizeof(blob) && revisionBytesWritten == sizeof(uint16_t);
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
    PreviousStoredSettingsBlob previousBlob{};
    size_t currentBytesRead = 0;
    size_t previousBytesRead = 0;

    if (storedSize == sizeof(blob)) {
        currentBytesRead = preferences.getBytes(SETTINGS_NVS_KEY, &blob, sizeof(blob));
    } else if (storedSize == sizeof(previousBlob)) {
        previousBytesRead = preferences.getBytes(SETTINGS_NVS_KEY, &previousBlob, sizeof(previousBlob));
    }

    const uint16_t storedDefaultsRevision = preferences.getUShort(SETTINGS_DEFAULTS_REVISION_KEY, 0);
    preferences.end();

    bool storedBlobValid = currentBytesRead == sizeof(blob) && blob.magic == SETTINGS_MAGIC && blob.schemaVersion == RUNTIME_SETTINGS_SCHEMA_VERSION && blob.payloadSize == sizeof(RuntimeSettings) && blob.checksum == settingsChecksum(blob.values);
    const bool previousBlobValid = previousBytesRead == sizeof(previousBlob) && previousBlob.magic == SETTINGS_MAGIC && previousBlob.schemaVersion == PREVIOUS_RUNTIME_SETTINGS_SCHEMA_VERSION && previousBlob.payloadSize == PREVIOUS_RUNTIME_SETTINGS_PAYLOAD_SIZE && previousBlob.checksum == settingsChecksumBytes(previousBlob.values, sizeof(previousBlob.values));
    bool schemaMigrated = false;

    if (!storedBlobValid && previousBlobValid) {
        blob.magic = SETTINGS_MAGIC;
        blob.schemaVersion = RUNTIME_SETTINGS_SCHEMA_VERSION;
        blob.payloadSize = sizeof(RuntimeSettings);
        blob.values = makeDefaultRuntimeSettings();
        memcpy(&blob.values, previousBlob.values, PREVIOUS_RUNTIME_SETTINGS_PAYLOAD_SIZE);
        blob.checksum = settingsChecksum(blob.values);
        storedBlobValid = true;
        schemaMigrated = true;
    }

    if (!storedBlobValid) {
        if (storedSize == 0) {
            lastSettingsResult = "Usando valores de fabrica";
        } else {
            const bool removed = clearRuntimeSettingsNvs();
            lastSettingsResult = removed ? "Configuracao anterior removida; valores de fabrica restaurados" : "Configuracao anterior invalida; limpeza da NVS falhou";
        }
        Serial.println(lastSettingsResult);
        return;
    }

    const bool defaultsRevisionPending = storedDefaultsRevision < RUNTIME_SETTINGS_DEFAULTS_REVISION;
    const bool runtimeDefaultsMigrated = defaultsRevisionPending && migrateRuntimeDefaults(blob.values, storedDefaultsRevision);
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
        lastSettingsResult = "Configuracao salva invalida; valores de fabrica restaurados";
        Serial.printf("%s: %s.\n", lastSettingsResult.c_str(), validationError.c_str());
        return;
    }

    settings = blob.values;
    settingsLoadedFromNvs = true;

    if (schemaMigrated || defaultsRevisionPending || previewLimitMigrated) {
        const bool migratedSaved = saveRuntimeSettingsToNvs(settings);

        if (schemaMigrated) {
            lastSettingsResult = migratedSaved ? "Configuracoes carregadas; esquema 4 migrado para 5" : "Configuracoes carregadas; migracao para esquema 5 nao persistida";
        } else if (runtimeDefaultsMigrated) {
            lastSettingsResult = migratedSaved ? "Configuracoes carregadas; novos padroes aplicados" : "Configuracoes carregadas; novos padroes nao persistidos";
        } else if (previewLimitMigrated) {
            lastSettingsResult = migratedSaved ? "Configuracoes carregadas; limite da previa ajustado" : "Configuracoes carregadas; ajuste da previa nao persistido";
        } else {
            lastSettingsResult = migratedSaved ? "Configuracoes carregadas; revisao de padroes registrada" : "Configuracoes carregadas; revisao de padroes nao persistida";
        }
    } else {
        lastSettingsResult = "Configuracoes carregadas da NVS";
    }

    Serial.println(lastSettingsResult);
    Serial.printf("Configuracoes NVS: ciclo=%lus; alerta=%u; reboot=%u; auto=%s.\n", static_cast<unsigned long>(HEALTH_CHECK_INTERVAL_MS / 1000UL), VIDEO_FAILURES_BEFORE_ALERT, VIDEO_FAILURES_BEFORE_REBOOT, ENABLE_ONVIF_AUTO_REBOOT ? "ativo" : "desativado");
}

void recordRuntimeSettingsContext(const char* source, uint8_t flags)
{
    PersistentLog::record(PersistentLog::Event::SettingsContext, PersistentLog::Outcome::Success, source, IPAddress(), 0, RUNTIME_SETTINGS_SCHEMA_VERSION, 0, SCAN_CONNECT_TIMEOUT_MS, flags, PersistentLog::Level::Info, true);
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
        PersistentLog::record(PersistentLog::Event::Incident, PersistentLog::Outcome::Started, incidentName(kind), camera.ip, camera.onvifPort, 0, 0, static_cast<uint32_t>(kind), PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true);
        return;
    }

    if (activeIncident == IncidentKind::SnapshotUnavailable && kind == IncidentKind::CameraOffline) {
        activeIncident = kind;
        consecutiveSuccesses = 0;
        PersistentLog::record(PersistentLog::Event::Incident, PersistentLog::Outcome::Started, incidentName(kind), camera.ip, camera.onvifPort, 0, millis() - incidentStartedAtMs, static_cast<uint32_t>(kind), PersistentLog::FlagScheduled, PersistentLog::Level::Error, true);

        if (incidentAlertSent) {
            incidentAlertSent = false;
            pendingIncidentEmail = true;
        }

        return;
    }

    if (activeIncident == IncidentKind::CameraOffline && kind == IncidentKind::SnapshotUnavailable && !incidentAlertSent) {
        activeIncident = kind;
        consecutiveSuccesses = 0;
    }
}

void queueIncidentAlert()
{
    if (ENABLE_EMAIL_ALERTS && activeIncident != IncidentKind::None && !incidentAlertSent) {
        pendingIncidentEmail = true;
    }
}

void resolveActiveIncident()
{
    if (activeIncident == IncidentKind::None) {
        return;
    }

    const IncidentKind resolvedIncident = activeIncident;
    const uint32_t incidentDurationMs = incidentStartedAtMs == 0 ? 0 : millis() - incidentStartedAtMs;

    if (incidentAlertSent && ENABLE_EMAIL_ALERTS) {
        recoveryIncident = activeIncident;
        recoveryIncidentDurationMs = millis() - incidentStartedAtMs;
        pendingRecoveryEmail = true;
    }

    pendingIncidentEmail = false;
    incidentAlertSent = false;
    activeIncident = IncidentKind::None;
    incidentStartedAtMs = 0;
    PersistentLog::record(PersistentLog::Event::Incident, PersistentLog::Outcome::Success, incidentName(resolvedIncident), camera.ip, camera.onvifPort, 0, incidentDurationMs, static_cast<uint32_t>(resolvedIncident), PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
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

    const bool shouldBlink = WiFi.status() != WL_CONNECTED || cameraState == CameraState::Offline || cameraState == CameraState::Recovering || activeIncident != IncidentKind::None;

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

    if (now - lastCameraAlertLedToggleAtMs >= CAMERA_ALERT_LED_BLINK_INTERVAL_MS) {
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
        Serial.printf("Memoria Bluetooth liberada: %u bytes.\n", bluetoothMemoryReleasedBytes);
    } else if (bluetoothMemoryReleaseResult == ESP_ERR_NOT_FOUND) {
        Serial.println("Memoria Bluetooth ja estava liberada ou nao foi reservada.");
    } else {
        Serial.printf("Nao foi possivel liberar memoria Bluetooth: %d\n", static_cast<int>(bluetoothMemoryReleaseResult));
    }

    Serial.printf("Heap Bluetooth: antes=%u depois=%u diferenca=%u bytes\n", heapBeforeBluetoothReleaseBytes, heapAfterBluetoothReleaseBytes, bluetoothMemoryReleasedBytes);
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

bool cameraCredentialsConfigured()
{
    return settings.cameraUsername[0] != '\0';
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
    digestOk = digestOk && mbedtls_sha1_update_ret(&context, nonce, sizeof(nonce)) == 0;
    digestOk = digestOk && mbedtls_sha1_update_ret(&context, reinterpret_cast<const unsigned char*>(created.c_str()), created.length()) == 0;
    digestOk = digestOk && mbedtls_sha1_update_ret(&context, reinterpret_cast<const unsigned char*>(settings.cameraPassword), strlen(settings.cameraPassword)) == 0;
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
    header += htmlEscape(settings.cameraUsername);
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
    return cameraRebootGraceStartedAtMs != 0 && millis() - cameraRebootGraceStartedAtMs < ONVIF_REBOOT_GRACE_MS;
}

unsigned long cameraRebootGraceRemainingMs()
{
    if (!cameraRebootGraceActive()) {
        return 0;
    }

    return ONVIF_REBOOT_GRACE_MS - (millis() - cameraRebootGraceStartedAtMs);
}

void refreshCameraRebootWindow()
{
    const unsigned long now = millis();
    uint8_t activeCount = 0;

    for (uint8_t index = 0; index < cameraRebootAttemptsInWindow; index++) {
        const unsigned long acceptedAtMs = cameraRebootAcceptedAtMs[index];

        if (acceptedAtMs != 0 && now - acceptedAtMs < ONVIF_REBOOT_WINDOW_MS) {
            cameraRebootAcceptedAtMs[activeCount++] = acceptedAtMs;
        }
    }

    for (uint8_t index = activeCount; index < MAX_ONVIF_REBOOT_ATTEMPTS_MAX; index++) {
        cameraRebootAcceptedAtMs[index] = 0;
    }

    cameraRebootAttemptsInWindow = activeCount;
    cameraRebootWindowStartedAtMs = activeCount > 0 ? cameraRebootAcceptedAtMs[0] : 0;
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

void applyRuntimeSettings(const RuntimeSettings& previous)
{
    const bool credentialsChanged = strcmp(previous.cameraUsername, settings.cameraUsername) != 0 || strcmp(previous.cameraPassword, settings.cameraPassword) != 0;

    if (strcmp(previous.localHostname, settings.localHostname) != 0 && connectedServicesInitialized && WiFi.status() == WL_CONNECTED) {
        MDNS.end();
        mdnsAvailable = false;
        startMdns();
    }

    if (credentialsChanged || strcmp(previous.snapshotProfileMatch, settings.snapshotProfileMatch) != 0) {
        invalidateSnapshotTarget();
        if (credentialsChanged) {
            invalidateOnvifServiceCatalog();
        }
        lastSnapshotResolveAttemptAtMs = 0;
        lastSnapshotResolveStatusCode = 0;
        lastSnapshotResolveTransportFailure = false;
        lastSnapshotResult = credentialsChanged ? "Credenciais alteradas; aguardando novo teste JPEG" : "Perfil alterado; aguardando novo teste JPEG";
        resetCameraCheckSequences(lastSnapshotResult.c_str());
    }

    if (previous.cameraRtspPort != settings.cameraRtspPort) {
        camera.rtspPort = settings.cameraRtspPort;

        if (camera.valid) {
            saveCameraTarget(0, PersistentLog::FlagManual);
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

    if (previous.healthCheckIntervalMs != settings.healthCheckIntervalMs) {
        lastHealthCheckAtMs = millis();
    }

    if (credentialsChanged) {
        lastHealthCheckAtMs = millis() - HEALTH_CHECK_INTERVAL_MS;
    }

    if (previous.onvifRebootWindowMs != settings.onvifRebootWindowMs || previous.onvifRebootCooldownMs != settings.onvifRebootCooldownMs || previous.maxOnvifRebootAttemptsPerWindow != settings.maxOnvifRebootAttemptsPerWindow) {
        refreshCameraRebootWindow();
    }
}

void setState(CameraState newState, const String& event)
{
    const bool stateChanged = cameraState != newState;

    if (stateChanged) {
        cameraState = newState;
        lastStateChangeAtMs = millis();

        PersistentLog::Level level = PersistentLog::Level::Info;

        if (newState == CameraState::Degraded || newState == CameraState::Recovering) {
            level = PersistentLog::Level::Warning;
        } else if (newState == CameraState::Offline) {
            level = PersistentLog::Level::Error;
        }

        PersistentLog::record(PersistentLog::Event::CameraState, PersistentLog::Outcome::Info, stateName(newState), camera.ip, camera.onvifPort, 0, 0, static_cast<uint32_t>(newState), PersistentLog::FlagScheduled, level, true);
    }

    lastEvent = currentTimestamp() + " - " + event;
    Serial.printf("ESTADO: %s | %s\n", stateName(cameraState), event.c_str());
}

bool resendConfigured()
{
    return String(RESEND_API_KEY).startsWith("re_") && String(RESEND_API_KEY).indexOf("COLE_SUA") < 0 && String(RESEND_FROM).indexOf("seudominio") < 0 && String(ALERT_EMAIL).indexOf("exemplo.com") < 0;
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

bool sendResendEmail(const String& subject, const String& html)
{
    lastEmailAttemptAtMs = millis();
    const unsigned long startedAtMs = millis();

    if (!resendConfigured()) {
        lastEmailResult = "Resend nao configurado em include/secrets.h";
        Serial.println(lastEmailResult);
        emailErrors++;
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Skipped, "resend_not_configured", IPAddress(), 443, 0, millis() - startedAtMs, 0, PersistentLog::FlagOutbound);
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        lastEmailResult = "Wi-Fi desconectado; envio adiado";
        emailErrors++;
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Skipped, "wifi_disconnected", IPAddress(), 443, 0, millis() - startedAtMs, 0, PersistentLog::FlagOutbound);
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
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Failure, "https_begin_failed", IPAddress(), 443, -1, millis() - startedAtMs, 0, PersistentLog::FlagOutbound);
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
        responseBody = "Resposta da API excedeu o limite seguro";
    } else if (statusCode > 0 && !responseRead) {
        responseBody = "Resposta da API indisponivel";
    }

    if (statusCode >= 200 && statusCode < 300) {
        emailsSent++;
        lastEmailResult = currentTimestamp() + " - enviado; HTTP " + String(statusCode) + "; " + responseBody;

        Serial.println(lastEmailResult);
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Success, "resend_email", IPAddress(), 443, statusCode, millis() - startedAtMs, static_cast<uint32_t>(payload.length()), PersistentLog::FlagOutbound);
        return true;
    }

    emailErrors++;
    lastEmailResult = currentTimestamp() + " - erro; HTTP " + String(statusCode) + "; " + responseBody;

    Serial.println(lastEmailResult);
    PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Failure, "resend_email", IPAddress(), 443, statusCode, millis() - startedAtMs, static_cast<uint32_t>(payload.length()), PersistentLog::FlagOutbound);
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

void appendDetailsTable(String& html)
{
    html += "<tr><td style=\"padding:24px 28px 0\">" "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\" style=\"border-collapse:separate;" "border-spacing:0;border:1px solid #e3e9f0;border-radius:10px;" "overflow:hidden\">";

    uint8_t index = 0;

    const auto row = [&](const char* label, const String& value) {
        appendDetailRow(html, label, value, index);
        index++;
    };

    String snapshotDescription = lastSnapshotProbe.detail;

    if (lastSnapshotProbe.jpegValid) {
        snapshotDescription = String("JPEG válido · ") + String(lastSnapshotProbe.bytesRead) + " bytes · " + String(lastSnapshotProbe.latencyMs) + " ms";
    }

    row("Câmera", CAMERA_NAME);
    row("Estado", stateLabel(cameraState));
    row("Endereço IP", camera.valid ? camera.ip.toString() : "não localizado");
    row("Endpoint ONVIF", camera.onvifUrl.isEmpty() ? String("desconhecido") : camera.onvifUrl);
    row("Teste de imagem", snapshotDescription);
    row("Teste ONVIF", describeProbe(lastOnvifProbe, "HTTP"));
    row("Teste RTSP", lastRtspProbeAttempted ? describeProbe(lastRtspProbe, "RTSP") : lastOnvifProbe.protocolResponded ? String("dispensado; ONVIF respondeu") : String("não executado; aguardando câmera e ONVIF"));
    row("Falhas de imagem consecutivas", String(consecutiveFailures));
    row("Falhas de controle consecutivas", String(consecutiveControlFailures));
    row("Recuperação ONVIF", lastCameraRebootResult);
    row("Verificações", String(totalHealthChecks) + " no total · " + String(failedHealthChecks) + " com falha");
    row("ESP32", WiFi.localIP().toString() + " · " + WiFi.SSID() + " · " + String(WiFi.RSSI()) + " dBm");
    row("Ligado há", durationText(millis()));
    row("Horário (UTC)", currentTimestamp());

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

String buildEmailHtml(const EmailTheme& theme, const char* badge, const char* title, const char* intro, const String& callout)
{
    String html;
    html.reserve(10240);

    html += "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">" "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" "<meta name=\"x-apple-disable-message-reformatting\"></head>" "<body style=\"margin:0;padding:0;background:#eef2f7\">" "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\" style=\"background:#eef2f7;" "padding:26px 12px\"><tr><td align=\"center\">" "<table role=\"presentation\" width=\"600\" cellpadding=\"0\" " "cellspacing=\"0\" border=\"0\" style=\"width:100%;max-width:600px;" "background:#ffffff;border:1px solid #dfe6ee;border-radius:14px;" "overflow:hidden;font-family:-apple-system,BlinkMacSystemFont," "'Segoe UI',Roboto,Helvetica,Arial,sans-serif\">";

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

    appendDetailsTable(html);

    html += "<tr><td style=\"padding:24px 28px 0\">" "<a href=\"http://";
    html += WiFi.localIP().toString();
    html += "\" style=\"display:inline-block;background:#0f1b2b;color:#ffffff;" "text-decoration:none;padding:12px 22px;border-radius:9px;" "font-size:14px;font-weight:600\">Abrir o painel do dispositivo</a>" "</td></tr>";

    html += "<tr><td style=\"padding:24px 28px 26px\">" "<div style=\"border-top:1px solid #edf2f7;padding-top:16px;" "font-size:11.5px;line-height:1.6;color:#94a3b8\">" "Mensagem automática do ESP32 Camera Watchdog · firmware ";
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

    const bool snapshotOnly = activeIncident == IncidentKind::SnapshotUnavailable;
    const uint8_t snapshotFailureCount = lastSnapshotProbe.conclusive ? consecutiveFailures : consecutiveInconclusiveChecks;

    String subject = snapshotOnly ? String("[Snapshot indisponivel] ") : String("[Camera offline] ");
    subject += CAMERA_NAME;

    String callout;
    callout.reserve(640);

    if (snapshotOnly) {
        callout = "A câmera ainda responde aos serviços de controle, mas falhou " "em <strong>";
        callout += snapshotFailureCount;
        callout += "</strong> verificações consecutivas do snapshot. Último teste: ";
        callout += htmlEscape(lastSnapshotProbe.detail);
        callout += ".";
    } else {
        callout = "O ESP32 não conseguiu obter imagem nem confirmar resposta " "ONVIF/RTSP. Falhas consecutivas: imagem <strong>";
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

    const String html = buildEmailHtml(EMAIL_THEME_ALERT, snapshotOnly ? "SEM SNAPSHOT" : "OFFLINE", snapshotOnly ? "A câmera parou de entregar snapshots JPEG" : "A câmera parou de responder", snapshotOnly ? "O serviço de controle continua acessível, porém o ESP32 não " "recebeu um JPEG completo e válido nas últimas verificações." : "A indisponibilidade foi confirmada por testes independentes de " "imagem, ONVIF e RTSP.", callout);

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

    const bool recoveredSnapshot = recoveryIncident == IncidentKind::SnapshotUnavailable;

    String subject = recoveredSnapshot ? String("[Snapshot recuperado] ") : String("[Camera recuperada] ");
    subject += CAMERA_NAME;

    String callout = "Duração aproximada do incidente: <strong>";
    callout += durationText(recoveryIncidentDurationMs);
    callout += (recoveredSnapshot ? "</strong>. O teste atual recebeu um JPEG completo e válido." : "</strong>. As verificações atuais confirmaram que a câmera voltou a responder.");

    if (lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Última recuperação ONVIF: ";
        callout += htmlEscape(lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(EMAIL_THEME_OK, "RECUPERADA", recoveredSnapshot ? "O snapshot JPEG voltou a funcionar" : "A câmera voltou a responder", "Duas verificações consecutivas confirmaram a recuperação. O " "monitoramento normal foi retomado.", callout);

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
    if (!ENABLE_EMAIL_ALERTS || !EMAIL_ON_IP_CHANGE || oldIp.isEmpty() || oldIp == newIp) {
        return true;
    }

    String subject = "[Camera mudou de IP] ";
    subject += CAMERA_NAME;

    String callout = "<strong>";
    callout += htmlEscape(oldIp);
    callout += "</strong> &rarr; <strong>";
    callout += htmlEscape(newIp);
    callout += "</strong>";

    const String html = buildEmailHtml(EMAIL_THEME_INFO, "NOVO IP", "A câmera mudou de endereço", "O ESP32 localizou a câmera em um novo IP e já atualizou o endpoint " "monitorado. Atualize favoritos e gravadores que apontem para o " "endereço antigo.", callout);

    return sendResendEmail(subject, html);
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
