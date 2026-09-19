#include "firmware_internal.h"

namespace CameraWatchdog {

const char FIRMWARE_VERSION[] = "0.17.0";

constexpr char RESEND_API_URL[] = "https://api.resend.com/emails";
constexpr char SETTINGS_NVS_NAMESPACE[] = "camcfg";
constexpr char SETTINGS_NVS_KEY[] = "runtime";
constexpr char SETTINGS_DEFAULTS_REVISION_KEY[] = "defaults_rev";
constexpr uint32_t SETTINGS_MAGIC = 0x43573035UL;
constexpr unsigned long ESP_SNTP_REFERENCE_MAX_AGE_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t GLOBAL_SETTINGS_MAGIC = 0x43573036UL;
constexpr uint32_t CAMERA_RECORD_MAGIC = 0x43414D36UL;
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

struct CameraRecordHeader {
    uint32_t magic;
    uint32_t id;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    uint16_t urlBytes;
    uint16_t uuidBytes;
    RuntimeSettings values;
    uint32_t checksum;
};

struct MigrationSettingsV5 {
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
    char localHostname[LOCAL_HOSTNAME_MAX_LENGTH + 1];
    char cameraUsername[CAMERA_USERNAME_MAX_LENGTH + 1];
    char cameraPassword[CAMERA_PASSWORD_MAX_LENGTH + 1];
};

struct MigrationSettingsBlob {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadSize;
    MigrationSettingsV5 values;
    uint32_t checksum;
};

GlobalSettings globalSettings = makeDefaultGlobalSettings();
std::unique_ptr<CameraContext> cameras;
size_t cameraCount = 0;
uint32_t registryRevision = 1;
String lastRegistryResult = "Cadastro ainda nao carregado";
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
uint32_t loggedSntpSynchronizationGeneration = 0;
esp_err_t bluetoothMemoryReleaseResult = ESP_ERR_NOT_SUPPORTED;
esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease = ESP_BT_CONTROLLER_STATUS_NUM;

String lastDiscoveryResult = "Ainda nao executada";
String lastScanResult = "Ainda nao executada";

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
    CHECK_SETTING_RANGE(tcpConnectTimeoutMs, TCP_CONNECT_TIMEOUT_MIN_MS, TCP_CONNECT_TIMEOUT_MAX_MS, "Timeout TCP fora do limite");
    CHECK_SETTING_RANGE(serviceResponseTimeoutMs, SERVICE_RESPONSE_TIMEOUT_MIN_MS, SERVICE_RESPONSE_TIMEOUT_MAX_MS, "Timeout de protocolo fora do limite");
    CHECK_SETTING_RANGE(snapshotHttpTimeoutMs, SNAPSHOT_HTTP_TIMEOUT_MIN_MS, SNAPSHOT_HTTP_TIMEOUT_MAX_MS, "Timeout do JPEG fora do limite");
    CHECK_SETTING_RANGE(snapshotResolveRetryIntervalMs, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS, "Intervalo para resolver snapshot fora do limite");
    CHECK_SETTING_RANGE(snapshotFailuresBeforeReresolve, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX, "Falhas para resolver novamente o snapshot fora do limite");
    CHECK_SETTING_RANGE(feedMinIntervalMs, FEED_MIN_INTERVAL_MIN_MS, FEED_MIN_INTERVAL_MAX_MS, "Intervalo minimo da previa fora do limite");
    CHECK_SETTING_RANGE(feedTargetCycleMs, FEED_TARGET_CYCLE_MIN_MS, FEED_TARGET_CYCLE_MAX_MS, "Ciclo da previa fora do limite");
#undef CHECK_SETTING_RANGE

    if (values.enableOnvifAutoReboot > 1 || values.enableEmailAlerts > 1 || values.emailOnIpChange > 1) {
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

bool validateGlobalSettings(const GlobalSettings& values, String& error)
{
    if (!localHostnameValid(values.localHostname, sizeof(values.localHostname))) {
        error = "Endereco local invalido; use letras, numeros e hifens";
        return false;
    }

#define CHECK_SETTING_RANGE(field, minimum, maximum, message) \
    if (!settingInRange(values.field, minimum, maximum)) { \
        error = message; \
        return false; \
    }

    CHECK_SETTING_RANGE(wifiConnectTimeoutMs, WIFI_CONNECT_TIMEOUT_MIN_MS, WIFI_CONNECT_TIMEOUT_MAX_MS, "Timeout de Wi-Fi fora do limite");
    CHECK_SETTING_RANGE(discoveryTimeoutMs, DISCOVERY_TIMEOUT_MIN_MS, DISCOVERY_TIMEOUT_MAX_MS, "Timeout de descoberta fora do limite");
    CHECK_SETTING_RANGE(rediscoveryIntervalMs, REDISCOVERY_INTERVAL_MIN_MS, REDISCOVERY_INTERVAL_MAX_MS, "Intervalo de redescoberta fora do limite");
    CHECK_SETTING_RANGE(subnetScanIntervalMs, SUBNET_SCAN_INTERVAL_MIN_MS, SUBNET_SCAN_INTERVAL_MAX_MS, "Intervalo de varredura fora do limite");
    CHECK_SETTING_RANGE(emailRetryIntervalMs, EMAIL_RETRY_INTERVAL_MIN_MS, EMAIL_RETRY_INTERVAL_MAX_MS, "Intervalo de e-mail fora do limite");
    CHECK_SETTING_RANGE(scanConnectTimeoutMs, SCAN_CONNECT_TIMEOUT_MIN_MS, SCAN_CONNECT_TIMEOUT_MAX_MS, "Timeout da varredura fora do limite");
    CHECK_SETTING_RANGE(cameraAlertLedBlinkIntervalMs, CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS, CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS, "Intervalo do LED fora do limite");
    CHECK_SETTING_RANGE(statusRefreshIntervalMs, STATUS_REFRESH_INTERVAL_MIN_MS, STATUS_REFRESH_INTERVAL_MAX_MS, "Atualizacao do painel fora do limite");
#undef CHECK_SETTING_RANGE
    if (values.enableAutomaticSubnetScan > 1 || values.enableCameraAlertLed > 1) {
        error = "Opcao global booleana invalida";
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
    if (url.length() > 1024 || uuid.length() > 256)
        return false;
    const size_t size = sizeof(CameraRecordHeader) + url.length() + 1 + uuid.length() + 1;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]());
    if (!bytes)
        return false;
    CameraRecordHeader* header = reinterpret_cast<CameraRecordHeader*>(bytes.get());
    header->magic = CAMERA_RECORD_MAGIC;
    header->id = ctx.id;
    header->schemaVersion = RUNTIME_SETTINGS_SCHEMA_VERSION;
    header->payloadSize = sizeof(values);
    header->urlBytes = url.length() + 1;
    header->uuidBytes = uuid.length() + 1;
    header->values = values;
    memcpy(bytes.get() + sizeof(*header), url.c_str(), header->urlBytes);
    memcpy(bytes.get() + sizeof(*header) + header->urlBytes, uuid.c_str(), header->uuidBytes);
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

CameraContext* registerCameraTarget(const CameraTarget& target, String& error)
{
    if (!target.valid || target.onvifUrl.isEmpty()) {
        error = "Endpoint ONVIF invalido";
        return nullptr;
    }
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        const bool sameIdentity = !target.endpointUuid.isEmpty() && !ctx->camera.endpointUuid.isEmpty() && target.endpointUuid.equalsIgnoreCase(ctx->camera.endpointUuid);
        const bool sameEndpoint = target.onvifUrl == ctx->camera.onvifUrl;
        const bool identityConflict = !target.endpointUuid.isEmpty() && !ctx->camera.endpointUuid.isEmpty() && !sameIdentity;
        if (sameEndpoint && identityConflict) {
            ctx->camera.valid = false;
            ctx->camera.onvifUrl = String();
            invalidateSnapshotTarget(*ctx);
            invalidateOnvifServiceCatalog(*ctx);
            resetCameraCheckSequences(*ctx, "Endereco ocupado por outra camera; aguardando redescoberta");
            ctx->lastHealthCheckAtMs = 0;
            setState(*ctx, CameraState::Discovering, "Identidade ONVIF mudou neste endereco; acesso suspenso");
            if (!persistCamera(*ctx)) {
                error = "Falha ao persistir conflito de identidade ONVIF";
                return nullptr;
            }
            registryRevision++;
        }
        if (!sameIdentity && !(sameEndpoint && !identityConflict))
            continue;

        const bool changed = ctx->camera.onvifUrl != target.onvifUrl || (ctx->camera.endpointUuid.isEmpty() && !target.endpointUuid.isEmpty());
        ctx->lastDiscoverySeenAtMs = millis();
        if (!changed)
            return ctx;
        const CameraTarget previous = ctx->camera;
        ctx->camera = target;
        ctx->camera.rtspPort = ctx->settings.cameraRtspPort;
        if (!persistCamera(*ctx)) {
            ctx->camera = previous;
            error = "Falha ao persistir o endpoint; cadastro anterior preservado";
            return nullptr;
        }
        invalidateSnapshotTarget(*ctx);
        invalidateOnvifServiceCatalog(*ctx);
        resetCameraCheckSequences(*ctx, "Endpoint atualizado; aguardando verificacao");
        ctx->lastSnapshotResolveAttemptAtMs = 0;
        ctx->lastHealthCheckAtMs = 0;
        if (previous.valid && previous.ip != target.ip) {
            ctx->previousCameraIp = previous.ip.toString();
            ctx->pendingIpChangeEmail = true;
        }
        registryRevision++;
        return ctx;
    }

    if (!cameraRegistryFullyLoaded) {
        error = "Cadastro NVS incompleto; novos registros suspensos para evitar duplicacao";
        return nullptr;
    }
    const size_t needed = sizeof(CameraContext) + target.onvifUrl.length() + target.endpointUuid.length() + target.scopes.length() + 2048;
    if (nextCameraId == 0 || ESP.getFreeHeap() < needed + CAMERA_OPERATION_HEAP_RESERVE || ESP.getMaxAllocHeap() < sizeof(CameraContext) + 4096) {
        error = "Memoria insuficiente para cadastrar outra camera mantendo o monitoramento";
        return nullptr;
    }
    std::unique_ptr<CameraContext> entry(new (std::nothrow) CameraContext());
    if (!entry) {
        error = "Falha ao alocar o estado da camera";
        return nullptr;
    }
    entry->id = nextCameraId;
    entry->registeredAtMs = millis();
    entry->lastStateChangeAtMs = entry->registeredAtMs;
    entry->lastDiscoverySeenAtMs = entry->registeredAtMs;
    entry->camera = target;
    entry->camera.rtspPort = entry->settings.cameraRtspPort;
    copySettingText(entry->settings.cameraName, String("Camera ") + entry->id + " - " + target.ip.toString());
    if (!persistCamera(*entry)) {
        error = "NVS sem espaco ou falha na gravacao; camera nao cadastrada";
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
    lastRegistryResult = String("Camera cadastrada: ") + result->settings.cameraName;
    return result;
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
    if (storage.begin(CAMERAS_NVS_NAMESPACE, true)) {
        lastId = storage.getUInt("last_id", 0);
        nvs_stats_t stats{};
        if (nvs_get_stats(nullptr, &stats) != ESP_OK || lastId > stats.total_entries) {
            lastRegistryResult = "Indice de cameras invalido na NVS";
            cameraRegistryFullyLoaded = false;
            lastId = 0;
        }
        std::unique_ptr<CameraContext>* link = &cameras;
        for (uint32_t id = 1; id <= lastId; id++) {
            char key[12];
            snprintf(key, sizeof(key), "c%08lx", static_cast<unsigned long>(id));
            const size_t size = storage.getBytesLength(key);
            if (size < sizeof(CameraRecordHeader) + 2 || size > sizeof(CameraRecordHeader) + 1282) {
                lastRegistryResult = String("Cadastro invalido: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            if (ESP.getFreeHeap() < sizeof(CameraContext) + size + CAMERA_OPERATION_HEAP_RESERVE) {
                lastRegistryResult = "Memoria insuficiente para carregar todos os cadastros; NVS preservada";
                cameraRegistryFullyLoaded = false;
                break;
            }
            std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[size]);
            if (!bytes || storage.getBytes(key, bytes.get(), size) != size) {
                lastRegistryResult = String("Falha ao carregar camera ") + id;
                cameraRegistryFullyLoaded = false;
                break;
            }
            CameraRecordHeader* record = reinterpret_cast<CameraRecordHeader*>(bytes.get());
            const uint32_t checksum = record->checksum;
            record->checksum = 0;
            if (record->magic != CAMERA_RECORD_MAGIC || record->id != id || record->schemaVersion != RUNTIME_SETTINGS_SCHEMA_VERSION || record->payloadSize != sizeof(RuntimeSettings) || record->urlBytes == 0 || record->uuidBytes == 0 || sizeof(*record) + record->urlBytes + record->uuidBytes != size || settingsChecksumBytes(bytes.get(), size) != checksum) {
                lastRegistryResult = String("Integridade invalida: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            const char* url = reinterpret_cast<const char*>(bytes.get() + sizeof(*record));
            const char* uuid = url + record->urlBytes;
            if (url[record->urlBytes - 1] != '\0' || uuid[record->uuidBytes - 1] != '\0') {
                lastRegistryResult = String("Endereco invalido: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            ParsedUrl parsed;
            String error;
            const bool endpointKnown = url[0] != '\0';
            if ((endpointKnown && (!parseHttpUrl(url, parsed) || parsed.secure)) || !validateRuntimeSettings(record->values, error)) {
                lastRegistryResult = String("Configuracao invalida: camera ") + id;
                cameraRegistryFullyLoaded = false;
                continue;
            }
            std::unique_ptr<CameraContext> entry(new (std::nothrow) CameraContext());
            if (!entry) {
                lastRegistryResult = "Memoria insuficiente ao carregar camera";
                cameraRegistryFullyLoaded = false;
                break;
            }
            entry->id = id;
            entry->settings = record->values;
            entry->settingsLoadedFromNvs = true;
            entry->registeredAtMs = millis();
            entry->lastStateChangeAtMs = entry->registeredAtMs;
            entry->camera.valid = endpointKnown;
            entry->camera.ip = parsed.ip;
            entry->camera.onvifPort = parsed.port;
            entry->camera.rtspPort = entry->settings.cameraRtspPort;
            entry->camera.onvifPath = parsed.path;
            entry->camera.onvifUrl = url;
            entry->camera.endpointUuid = uuid;
            *link = std::move(entry);
            link = &(*link)->next;
            cameraCount++;
        }
        storage.end();
    }
    nextCameraId = lastId + 1;

    if (!globalValid && cameraRegistryFullyLoaded) {
        MigrationSettingsBlob migration{};
        bool migrate = false;
        if (storage.begin(SETTINGS_NVS_NAMESPACE, true)) {
            migrate = storage.getBytesLength(SETTINGS_NVS_KEY) == sizeof(migration) && storage.getBytes(SETTINGS_NVS_KEY, &migration, sizeof(migration)) == sizeof(migration) && migration.magic == SETTINGS_MAGIC && migration.schemaVersion == 5 && migration.payloadSize == sizeof(MigrationSettingsV5) && migration.checksum == settingsChecksum(migration.values);
            storage.end();
        }
        std::unique_ptr<CameraContext> entry(new (std::nothrow) CameraContext());
        if (!entry) {
            lastRegistryResult = "Memoria insuficiente para migrar o cadastro";
            return;
        }
        entry->id = 1;
        entry->settings = makeDefaultRuntimeSettings(true);
        if (migrate) {
            memcpy(&globalSettings.cameraAlertLedBlinkIntervalMs, &migration.values.cameraAlertLedBlinkIntervalMs, sizeof(globalSettings.cameraAlertLedBlinkIntervalMs));
            memcpy(&globalSettings.discoveryTimeoutMs, &migration.values.discoveryTimeoutMs, sizeof(globalSettings.discoveryTimeoutMs));
            memcpy(&globalSettings.emailRetryIntervalMs, &migration.values.emailRetryIntervalMs, sizeof(globalSettings.emailRetryIntervalMs));
            memcpy(&globalSettings.enableAutomaticSubnetScan, &migration.values.enableAutomaticSubnetScan, sizeof(globalSettings.enableAutomaticSubnetScan));
            memcpy(&globalSettings.enableCameraAlertLed, &migration.values.enableCameraAlertLed, sizeof(globalSettings.enableCameraAlertLed));
            memcpy(&globalSettings.localHostname, &migration.values.localHostname, sizeof(globalSettings.localHostname));
            memcpy(&globalSettings.rediscoveryIntervalMs, &migration.values.rediscoveryIntervalMs, sizeof(globalSettings.rediscoveryIntervalMs));
            memcpy(&globalSettings.scanConnectTimeoutMs, &migration.values.scanConnectTimeoutMs, sizeof(globalSettings.scanConnectTimeoutMs));
            memcpy(&globalSettings.statusRefreshIntervalMs, &migration.values.statusRefreshIntervalMs, sizeof(globalSettings.statusRefreshIntervalMs));
            memcpy(&globalSettings.subnetScanIntervalMs, &migration.values.subnetScanIntervalMs, sizeof(globalSettings.subnetScanIntervalMs));
            memcpy(&globalSettings.wifiConnectTimeoutMs, &migration.values.wifiConnectTimeoutMs, sizeof(globalSettings.wifiConnectTimeoutMs));
            memcpy(&entry->settings.cameraName, &migration.values.cameraName, sizeof(entry->settings.cameraName));
            memcpy(&entry->settings.cameraPassword, &migration.values.cameraPassword, sizeof(entry->settings.cameraPassword));
            memcpy(&entry->settings.cameraRtspPort, &migration.values.cameraRtspPort, sizeof(entry->settings.cameraRtspPort));
            memcpy(&entry->settings.cameraUsername, &migration.values.cameraUsername, sizeof(entry->settings.cameraUsername));
            memcpy(&entry->settings.emailOnIpChange, &migration.values.emailOnIpChange, sizeof(entry->settings.emailOnIpChange));
            memcpy(&entry->settings.enableEmailAlerts, &migration.values.enableEmailAlerts, sizeof(entry->settings.enableEmailAlerts));
            memcpy(&entry->settings.enableOnvifAutoReboot, &migration.values.enableOnvifAutoReboot, sizeof(entry->settings.enableOnvifAutoReboot));
            memcpy(&entry->settings.failuresBeforeOffline, &migration.values.failuresBeforeOffline, sizeof(entry->settings.failuresBeforeOffline));
            memcpy(&entry->settings.failuresBeforeRediscovery, &migration.values.failuresBeforeRediscovery, sizeof(entry->settings.failuresBeforeRediscovery));
            memcpy(&entry->settings.feedMinIntervalMs, &migration.values.feedMinIntervalMs, sizeof(entry->settings.feedMinIntervalMs));
            memcpy(&entry->settings.feedTargetCycleMs, &migration.values.feedTargetCycleMs, sizeof(entry->settings.feedTargetCycleMs));
            memcpy(&entry->settings.healthCheckIntervalMs, &migration.values.healthCheckIntervalMs, sizeof(entry->settings.healthCheckIntervalMs));
            memcpy(&entry->settings.maxOnvifRebootAttemptsPerWindow, &migration.values.maxOnvifRebootAttemptsPerWindow, sizeof(entry->settings.maxOnvifRebootAttemptsPerWindow));
            memcpy(&entry->settings.onvifRebootCooldownMs, &migration.values.onvifRebootCooldownMs, sizeof(entry->settings.onvifRebootCooldownMs));
            memcpy(&entry->settings.onvifRebootGraceMs, &migration.values.onvifRebootGraceMs, sizeof(entry->settings.onvifRebootGraceMs));
            memcpy(&entry->settings.onvifRebootWindowMs, &migration.values.onvifRebootWindowMs, sizeof(entry->settings.onvifRebootWindowMs));
            memcpy(&entry->settings.serviceResponseTimeoutMs, &migration.values.serviceResponseTimeoutMs, sizeof(entry->settings.serviceResponseTimeoutMs));
            memcpy(&entry->settings.snapshotFailuresBeforeReresolve, &migration.values.snapshotFailuresBeforeReresolve, sizeof(entry->settings.snapshotFailuresBeforeReresolve));
            memcpy(&entry->settings.snapshotHttpTimeoutMs, &migration.values.snapshotHttpTimeoutMs, sizeof(entry->settings.snapshotHttpTimeoutMs));
            memcpy(&entry->settings.snapshotProfileMatch, &migration.values.snapshotProfileMatch, sizeof(entry->settings.snapshotProfileMatch));
            memcpy(&entry->settings.snapshotResolveRetryIntervalMs, &migration.values.snapshotResolveRetryIntervalMs, sizeof(entry->settings.snapshotResolveRetryIntervalMs));
            memcpy(&entry->settings.startupGraceMs, &migration.values.startupGraceMs, sizeof(entry->settings.startupGraceMs));
            memcpy(&entry->settings.successesBeforeRecovery, &migration.values.successesBeforeRecovery, sizeof(entry->settings.successesBeforeRecovery));
            memcpy(&entry->settings.tcpConnectTimeoutMs, &migration.values.tcpConnectTimeoutMs, sizeof(entry->settings.tcpConnectTimeoutMs));
            memcpy(&entry->settings.videoFailuresBeforeAlert, &migration.values.videoFailuresBeforeAlert, sizeof(entry->settings.videoFailuresBeforeAlert));
            memcpy(&entry->settings.videoFailuresBeforeReboot, &migration.values.videoFailuresBeforeReboot, sizeof(entry->settings.videoFailuresBeforeReboot));
        }
        String error;
        if (!validateGlobalSettings(globalSettings, error) || !validateRuntimeSettings(entry->settings, error)) {
            lastRegistryResult = "Configuracao anterior invalida; dados preservados na NVS";
            return;
        }
        const bool targetLoaded = lastId == 0 && (loadCameraTarget(*entry) || loadCameraIpHint(*entry));
        if (targetLoaded) {
            entry->registeredAtMs = millis();
            entry->lastStateChangeAtMs = entry->registeredAtMs;
            if (!persistCamera(*entry)) {
                lastRegistryResult = "Falha ao migrar cadastro; configuracao anterior preservada";
                return;
            }
            cameras = std::move(entry);
            cameraCount = 1;
            nextCameraId = 2;
        }
        if (saveGlobalSettingsToNvs(globalSettings)) {
            settingsLoadedFromNvs = true;
            if ((targetLoaded || lastId != 0) && storage.begin(SETTINGS_NVS_NAMESPACE, false)) {
                storage.remove(SETTINGS_NVS_KEY);
                storage.remove(SETTINGS_DEFAULTS_REVISION_KEY);
                storage.end();
            }
            lastRegistryResult = migrate ? "Configuracao anterior migrada para cadastro de cameras" : "Cadastro inicial pronto";
        } else {
            lastRegistryResult = "Falha ao gravar configuracoes globais";
        }
    } else if (cameraRegistryFullyLoaded && cameraCount == lastId) {
        lastRegistryResult = String(cameraCount) + " cameras carregadas da NVS";
    }
    lastSettingsResult = settingsLoadedFromNvs ? "Configuracoes carregadas da NVS" : "Usando valores de fabrica";
}

void recordRuntimeSettingsContext(const char* source, uint8_t flags)
{
    PersistentLog::record(PersistentLog::Event::SettingsContext, PersistentLog::Outcome::Success, source, IPAddress(), 0, RUNTIME_SETTINGS_SCHEMA_VERSION, 0, globalSettings.scanConnectTimeoutMs, flags, PersistentLog::Level::Info, true);
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

    const IncidentKind resolvedIncident = ctx.activeIncident;
    const uint32_t incidentDurationMs = ctx.incidentStartedAtMs == 0 ? 0 : millis() - ctx.incidentStartedAtMs;

    if (ctx.incidentAlertSent && ctx.settings.enableEmailAlerts) {
        ctx.recoveryIncident = ctx.activeIncident;
        ctx.recoveryIncidentDurationMs = millis() - ctx.incidentStartedAtMs;
        ctx.pendingRecoveryEmail = true;
    }

    ctx.pendingIncidentEmail = false;
    ctx.incidentAlertSent = false;
    ctx.activeIncident = IncidentKind::None;
    ctx.incidentStartedAtMs = 0;
    PersistentLog::record(PersistentLog::Event::Incident, PersistentLog::Outcome::Success, incidentName(resolvedIncident), ctx.camera.ip, ctx.camera.onvifPort, 0, incidentDurationMs, static_cast<uint32_t>(resolvedIncident), PersistentLog::FlagScheduled, PersistentLog::Level::Info, true, 0, ctx.id);
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
        ctx.lastSnapshotResult = "Configuracao alterada; aguardando novo teste JPEG";
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
    Serial.printf("ESTADO: %s | %s\n", stateName(ctx.cameraState), event.c_str());
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

bool sendResendEmail(CameraContext& ctx, const String& subject, const String& html)
{
    ctx.lastEmailAttemptAtMs = millis();
    const unsigned long startedAtMs = millis();

    if (!resendConfigured()) {
        ctx.lastEmailResult = "Resend nao configurado em include/secrets.h";
        Serial.println(ctx.lastEmailResult);
        ctx.emailErrors++;
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Skipped, "resend_not_configured", IPAddress(), 443, 0, millis() - startedAtMs, 0, PersistentLog::FlagOutbound, PersistentLog::Level::Automatic, false, 0, ctx.id);
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        ctx.lastEmailResult = "Wi-Fi desconectado; envio adiado";
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
        ctx.lastEmailResult = "Falha ao inicializar cliente HTTPS";
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
        responseBody = "Resposta da API excedeu o limite seguro";
    } else if (statusCode > 0 && !responseRead) {
        responseBody = "Resposta da API indisponivel";
    }

    if (statusCode >= 200 && statusCode < 300) {
        ctx.emailsSent++;
        ctx.lastEmailResult = currentTimestamp() + " - enviado; HTTP " + String(statusCode) + "; " + responseBody;

        Serial.println(ctx.lastEmailResult);
        PersistentLog::record(PersistentLog::Event::EmailHttp, PersistentLog::Outcome::Success, "resend_email", IPAddress(), 443, statusCode, millis() - startedAtMs, static_cast<uint32_t>(payload.length()), PersistentLog::FlagOutbound, PersistentLog::Level::Automatic, false, 0, ctx.id);
        return true;
    }

    ctx.emailErrors++;
    ctx.lastEmailResult = currentTimestamp() + " - erro; HTTP " + String(statusCode) + "; " + responseBody;

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
        snapshotDescription = String("JPEG válido · ") + String(ctx.lastSnapshotProbe.bytesRead) + " bytes · " + String(ctx.lastSnapshotProbe.latencyMs) + " ms";
    }

    row("Câmera", ctx.settings.cameraName);
    row("Estado", stateLabel(ctx.cameraState));
    row("Endereço IP", ctx.camera.valid ? ctx.camera.ip.toString() : "não localizado");
    row("Endpoint ONVIF", ctx.camera.onvifUrl.isEmpty() ? String("desconhecido") : ctx.camera.onvifUrl);
    row("Teste de imagem", snapshotDescription);
    row("Teste ONVIF", describeProbe(ctx.lastOnvifProbe, "HTTP"));
    row("Teste RTSP", ctx.lastRtspProbeAttempted ? describeProbe(ctx.lastRtspProbe, "RTSP") : ctx.lastOnvifProbe.protocolResponded ? String("dispensado; ONVIF respondeu") : String("não executado; aguardando câmera e ONVIF"));
    row("Falhas de imagem consecutivas", String(ctx.consecutiveFailures));
    row("Falhas de controle consecutivas", String(ctx.consecutiveControlFailures));
    row("Recuperação ONVIF", ctx.lastCameraRebootResult);
    row("Verificações", String(ctx.totalHealthChecks) + " no total · " + String(ctx.failedHealthChecks) + " com falha");
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

String buildEmailHtml(CameraContext& ctx, const EmailTheme& theme, const char* badge, const char* title, const char* intro, const String& callout)
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

    appendDetailsTable(ctx, html);

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

bool sendIncidentAlert(CameraContext& ctx)
{
    if (ctx.activeIncident == IncidentKind::None) {
        ctx.pendingIncidentEmail = false;
        return true;
    }

    const bool snapshotOnly = ctx.activeIncident == IncidentKind::SnapshotUnavailable;
    const uint8_t snapshotFailureCount = ctx.lastSnapshotProbe.conclusive ? ctx.consecutiveFailures : ctx.consecutiveInconclusiveChecks;

    String subject = snapshotOnly ? String("[Snapshot indisponivel] ") : String("[Camera offline] ");
    subject += ctx.settings.cameraName;

    String callout;
    callout.reserve(640);

    if (snapshotOnly) {
        callout = "A câmera ainda responde aos serviços de controle, mas falhou " "em <strong>";
        callout += snapshotFailureCount;
        callout += "</strong> verificações consecutivas do snapshot. Último teste: ";
        callout += htmlEscape(ctx.lastSnapshotProbe.detail);
        callout += ".";
    } else {
        callout = "O ESP32 não conseguiu obter imagem nem confirmar resposta " "ONVIF/RTSP. Falhas consecutivas: imagem <strong>";
        callout += ctx.consecutiveFailures;
        callout += "</strong>, controle <strong>";
        callout += ctx.consecutiveControlFailures;
        callout += "</strong>.";
    }

    if (ctx.lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Recuperação automática: ";
        callout += htmlEscape(ctx.lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(ctx, EMAIL_THEME_ALERT, snapshotOnly ? "SEM SNAPSHOT" : "OFFLINE", snapshotOnly ? "A câmera parou de entregar snapshots JPEG" : "A câmera parou de responder", snapshotOnly ? "O serviço de controle continua acessível, porém o ESP32 não " "recebeu um JPEG completo e válido nas últimas verificações." : "A indisponibilidade foi confirmada por testes independentes de " "imagem, ONVIF e RTSP.", callout);

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

    String subject = recoveredSnapshot ? String("[Snapshot recuperado] ") : String("[Camera recuperada] ");
    subject += ctx.settings.cameraName;

    String callout = "Duração aproximada do incidente: <strong>";
    callout += durationText(ctx.recoveryIncidentDurationMs);
    callout += (recoveredSnapshot ? "</strong>. O teste atual recebeu um JPEG completo e válido." : "</strong>. As verificações atuais confirmaram que a câmera voltou a responder.");

    if (ctx.lastCameraRebootAttemptAtMs != 0) {
        callout += "<br><br>Última recuperação ONVIF: ";
        callout += htmlEscape(ctx.lastCameraRebootResult);
        callout += ".";
    }

    const String html = buildEmailHtml(ctx, EMAIL_THEME_OK, "RECUPERADA", recoveredSnapshot ? "O snapshot JPEG voltou a funcionar" : "A câmera voltou a responder", "Duas verificações consecutivas confirmaram a recuperação. O " "monitoramento normal foi retomado.", callout);

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

    String subject = "[Camera mudou de IP] ";
    subject += ctx.settings.cameraName;

    String callout = "<strong>";
    callout += htmlEscape(oldIp);
    callout += "</strong> &rarr; <strong>";
    callout += htmlEscape(newIp);
    callout += "</strong>";

    const String html = buildEmailHtml(ctx, EMAIL_THEME_INFO, "NOVO IP", "A câmera mudou de endereço", "O ESP32 localizou a câmera em um novo IP e já atualizou o endpoint " "monitorado. Atualize favoritos e gravadores que apontem para o " "endereço antigo.", callout);

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
