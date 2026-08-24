#include "firmware_internal.h"

namespace CameraWatchdog {

constexpr unsigned long ADMIN_RESTART_DELAY_MS = 1200UL;

bool cameraControlResponded(uint16_t contextId = 0)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();

    if (!camera.valid) {
        lastOnvifProbe = ServiceProbe{};
        lastRtspProbe = ServiceProbe{};
        lastRtspProbeAttempted = false;
        return false;
    }

    lastOnvifProbe = probeOnvif(camera, logContextId);

    if (lastOnvifProbe.protocolResponded) {
        lastRtspProbe = ServiceProbe{};
        lastRtspProbeAttempted = false;
        return true;
    }

    httpServer.handleClient();
    lastRtspProbe = probeRtsp(camera.ip, camera.rtspPort, logContextId);
    lastRtspProbeAttempted = true;

    return lastRtspProbe.protocolResponded;
}

void resetCameraCheckSequences(const char* snapshotDetail)
{
    consecutiveFailures = 0;
    consecutiveSuccesses = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    snapshotTargetFailureStreak = 0;
    resetSnapshotProfileFailover();
    lastOnvifProbe = ServiceProbe{};
    lastRtspProbe = ServiceProbe{};
    lastRtspProbeAttempted = false;
    lastSnapshotProbe = SnapshotProbe{};
    lastSnapshotProbe.detail = snapshotDetail;
    lastBrowserSnapshotDeliveredAtMs = 0;
    lastBrowserSnapshotBytes = 0;
    lastBrowserSnapshotLatencyMs = 0;
    lastHealthEvidenceSource = HealthEvidenceSource::None;
    lastHealthEvidenceAtMs = 0;
}

bool browserSnapshotRecentlyDelivered()
{
    return lastBrowserSnapshotDeliveredAtMs != 0 && millis() - lastBrowserSnapshotDeliveredAtMs <= HEALTH_CHECK_INTERVAL_MS;
}

void noteInconclusiveHealthCheck(const String& event)
{
    consecutiveSuccesses = 0;

    if (cameraState == CameraState::Online && activeIncident == IncidentKind::None) {
        lastEvent = currentTimestamp() + " - " + event;
        Serial.printf("VERIFICACAO ADIADA: %s\n", event.c_str());
        return;
    }

    setState(CameraState::Degraded, event);
}

void recordSuccessfulHealthCheck(HealthEvidenceSource evidenceSource, const char* confirmationEvent, const char* healthyEvent, const char* recoveredEvent)
{
    successfulHealthChecks++;
    lastHealthEvidenceSource = evidenceSource;
    lastHealthEvidenceAtMs = millis();
    consecutiveFailures = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;

    if (consecutiveSuccesses < 255) {
        consecutiveSuccesses++;
    }

    cameraRebootGraceStartedAtMs = 0;

    if (activeIncident != IncidentKind::None && consecutiveSuccesses < SUCCESSES_BEFORE_RECOVERY) {
        setState(CameraState::Degraded, confirmationEvent);
        return;
    }

    const bool recovered = activeIncident != IncidentKind::None;
    resolveActiveIncident();
    setState(CameraState::Online, recovered ? recoveredEvent : healthyEvent);
}

struct HealthCheckScheduleGuard {
    HealthCheckScheduleGuard(bool manualRequest, uint16_t logContextId) : startedAtMs(millis()), successfulBefore(successfulHealthChecks), failedBefore(failedHealthChecks), inconclusiveBefore(inconclusiveHealthChecks), manual(manualRequest), contextId(logContextId)
    {
        healthCheckInProgress = true;
    }

    ~HealthCheckScheduleGuard()
    {
        healthCheckInProgress = false;
        lastHealthCheckAtMs = millis();

        PersistentLog::Outcome outcome = PersistentLog::Outcome::Skipped;
        PersistentLog::Level level = PersistentLog::Level::Warning;
        const char* detail = "no_conclusive_result";

        if (successfulHealthChecks > successfulBefore) {
            outcome = PersistentLog::Outcome::Success;
            level = PersistentLog::Level::Info;
            detail = healthEvidenceSourceName(lastHealthEvidenceSource);
        } else if (failedHealthChecks > failedBefore) {
            outcome = PersistentLog::Outcome::Failure;
            level = PersistentLog::Level::Error;
            detail = stateName(cameraState);
        } else if (inconclusiveHealthChecks > inconclusiveBefore) {
            detail = "inconclusive";
        } else if (cameraRebootGraceActive()) {
            detail = "camera_reboot_grace";
        }

        PersistentLog::record(PersistentLog::Event::HealthCheck, outcome, detail, camera.ip, camera.onvifPort, lastSnapshotProbe.statusCode, millis() - startedAtMs, totalHealthChecks, manual ? PersistentLog::FlagManual : PersistentLog::FlagScheduled, level, false, contextId);
    }

    unsigned long startedAtMs;
    uint32_t successfulBefore;
    uint32_t failedBefore;
    uint32_t inconclusiveBefore;
    bool manual;
    uint16_t contextId;
};

void runHealthCheck(bool manualRequest = false, uint16_t contextId = 0)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    HealthCheckScheduleGuard scheduleGuard(manualRequest, logContextId);

    if (cameraRebootGraceStartedAtMs != 0 && !cameraRebootGraceActive()) {
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
        setState(CameraState::Recovering, String("Aguardando reinicializacao da camera; faltam cerca de ") + String(cameraRebootGraceRemainingMs() / 1000UL) + "s");
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
        recordSuccessfulHealthCheck(HealthEvidenceSource::PreviewJpeg, "Previa retornou; confirmando estabilidade", "Camera validada pela Previa recente", "Camera recuperada e JPEG validado pela Previa");
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        totalHealthChecks++;
        inconclusiveHealthChecks++;
        noteInconclusiveHealthCheck("Canal da camera ocupado; verificacao adiada");
        return;
    }

    totalHealthChecks++;

    if (!camera.valid) {
        discoverOnvifCamera(false, logContextId);
    }

    bool controlAvailable = cameraControlResponded(logContextId);
    const uint8_t nextControlFailure = consecutiveControlFailures < 255 ? consecutiveControlFailures + 1 : consecutiveControlFailures;

    if (!controlAvailable && nextControlFailure >= FAILURES_BEFORE_REDISCOVERY && (lastRediscoveryAtMs == 0 || millis() - lastRediscoveryAtMs >= REDISCOVERY_INTERVAL_MS)) {
        const bool rediscovered = discoverOnvifCamera(false, logContextId);

        if (rediscovered) {
            controlAvailable = cameraControlResponded(logContextId);
        }
    }

    if (!controlAvailable && nextControlFailure >= FAILURES_BEFORE_OFFLINE && ENABLE_AUTOMATIC_SUBNET_SCAN && (lastSubnetScanAtMs == 0 || millis() - lastSubnetScanAtMs >= SUBNET_SCAN_INTERVAL_MS)) {
        const bool foundByScan = scanSubnetForCamera(logContextId);

        if (foundByScan) {
            controlAvailable = cameraControlResponded(logContextId);
        }
    }

    const bool videoAvailable = probeCameraSnapshotJpeg(logContextId);

    if (videoAvailable) {
        Serial.printf("CAMERA OK: %s | JPEG=%u bytes %lums | " "ONVIF tcp=%d http=%d code=%d | " "RTSP tentado=%d tcp=%d protocolo=%d code=%d\n", camera.ip.toString().c_str(), static_cast<unsigned>(lastSnapshotProbe.bytesRead), static_cast<unsigned long>(lastSnapshotProbe.latencyMs), lastOnvifProbe.tcpConnected, lastOnvifProbe.protocolResponded, lastOnvifProbe.statusCode, lastRtspProbeAttempted, lastRtspProbe.tcpConnected, lastRtspProbe.protocolResponded, lastRtspProbe.statusCode);

        recordSuccessfulHealthCheck(HealthEvidenceSource::SnapshotJpeg, "Imagem retornou; confirmando estabilidade", "Camera e canal de imagem responderam ao teste", "Camera recuperada e JPEG validado");
        return;
    }

    const bool mediaServiceTransportFailure = !lastSnapshotProbe.conclusive && controlAvailable && lastSnapshotProbe.requestAttempted && lastSnapshotProbe.resolutionAttempted && lastSnapshotProbe.resolutionTransportFailure && lastOnvifProbe.protocolResponded;

    if (!lastSnapshotProbe.conclusive && controlAvailable) {

        if (mediaServiceTransportFailure) {
            if (consecutiveMediaServiceFailures < 255) {
                consecutiveMediaServiceFailures++;
            }
        } else if (lastSnapshotProbe.requestAttempted) {
            consecutiveMediaServiceFailures = 0;
        }

        if (mediaServiceTransportFailure && consecutiveMediaServiceFailures >= VIDEO_FAILURES_BEFORE_REBOOT) {
            lastSnapshotProbe.conclusive = true;
            lastSnapshotProbe.detail = String("Servico de midia ONVIF falhou repetidamente: ") + lastSnapshotProbe.detail;
            resetSnapshotProfileFailover();
        }
    }

    if (!lastSnapshotProbe.conclusive && controlAvailable) {
        consecutiveControlFailures = 0;

        if (lastSnapshotProbe.requestAttempted && consecutiveInconclusiveChecks < 255) {
            consecutiveInconclusiveChecks++;
        }

        inconclusiveHealthChecks++;

        noteInconclusiveHealthCheck(String("Monitor de snapshot inconclusivo: ") + lastSnapshotProbe.detail);
        return;
    }

    consecutiveInconclusiveChecks = 0;

    if (!lastSnapshotProbe.resolutionAttempted) {
        consecutiveMediaServiceFailures = 0;
    }

    consecutiveSuccesses = 0;
    failedHealthChecks++;

    if (lastSnapshotProbe.conclusive && consecutiveFailures < 255) {
        if (mediaServiceTransportFailure && consecutiveMediaServiceFailures > consecutiveFailures) {
            consecutiveFailures = consecutiveMediaServiceFailures;
        } else {
            consecutiveFailures++;
        }
    }

    if (controlAvailable) {
        consecutiveControlFailures = 0;

        if (millis() - bootAtMs < STARTUP_GRACE_MS) {
            setState(CameraState::Degraded, String("Camera em tolerancia inicial; imagem falhou: ") + lastSnapshotProbe.detail);
            return;
        }

        Serial.printf("SNAPSHOT FALHOU: tentativas=%u | IP=%s | HTTP=%d | %s\n", consecutiveFailures, camera.valid ? camera.ip.toString().c_str() : "desconhecido", lastSnapshotProbe.statusCode, lastSnapshotProbe.detail.c_str());

        if (consecutiveFailures >= VIDEO_FAILURES_BEFORE_ALERT) {
            beginOrEscalateIncident(IncidentKind::SnapshotUnavailable);
            queueIncidentAlert();
        }

        if (consecutiveFailures >= VIDEO_FAILURES_BEFORE_REBOOT) {
            const bool rebootEligible = lastOnvifProbe.protocolResponded && snapshotFailureAllowsAutomaticReboot();

            if (rebootEligible && requestCameraRebootOnvif(false, logContextId)) {
                return;
            }

            if (ENABLE_ONVIF_AUTO_REBOOT && !rebootEligible) {
                lastCameraRebootResult = currentTimestamp() + " - nao solicitado: ";

                if (!lastOnvifProbe.protocolResponded) {
                    lastCameraRebootResult += "servico ONVIF sem resposta para receber SystemReboot";
                } else if (snapshotProfileFailoverPending) {
                    lastCameraRebootResult += "perfil de snapshot alternativo ainda em teste";
                } else {
                    lastCameraRebootResult += "a falha indica autenticacao, recurso nao suportado ou configuracao";
                }
            }
        }

        setState(CameraState::Degraded, String("Camera responde, mas o snapshot falhou: ") + lastSnapshotProbe.detail);
        return;
    }

    if (consecutiveControlFailures < 255) {
        consecutiveControlFailures++;
    }

    Serial.printf("CAMERA SEM CONTROLE E SEM IMAGEM: video=%u controle=%u | IP=%s\n", consecutiveFailures, consecutiveControlFailures, camera.valid ? camera.ip.toString().c_str() : "desconhecido");

    if (consecutiveControlFailures < FAILURES_BEFORE_OFFLINE || millis() - bootAtMs < STARTUP_GRACE_MS) {
        setState(CameraState::Degraded, "Camera nao respondeu; confirmando indisponibilidade");
        return;
    }

    beginOrEscalateIncident(IncidentKind::CameraOffline);
    queueIncidentAlert();
    setState(CameraState::Offline, "Camera confirmada como indisponivel e sem imagem");
}

bool connectToWiFi()
{
    const unsigned long connectStartedAtMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(LOCAL_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);

    Serial.printf("Conectando a rede \"%s\"", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long startedAt = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
        updateCameraAlertLed();
        Serial.print('.');
        delay(500);
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Falha ao conectar ao Wi-Fi.");
        PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Failure, "station", IPAddress(), 0, static_cast<int>(WiFi.status()), millis() - connectStartedAtMs, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Error, true);
        return false;
    }

    Serial.printf("Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Mascara: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    wifiWasConnected = true;
    PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Success, "station", WiFi.gatewayIP(), 0, static_cast<int>(WiFi.status()), millis() - connectStartedAtMs, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);

    return true;
}

bool startMdns()
{
    mdnsAvailable = MDNS.begin(LOCAL_HOSTNAME);

    if (!mdnsAvailable) {
        Serial.println("Falha ao iniciar mDNS; painel disponivel pelo IP.");
        return false;
    }

    MDNS.addService("http", "tcp", HTTP_SERVER_PORT);
    Serial.printf("mDNS: http://%s.local/\n", LOCAL_HOSTNAME);
    return true;
}

void initializeConnectedServices()
{
    if (connectedServicesInitialized || WiFi.status() != WL_CONNECTED) {
        return;
    }

    connectedServicesInitialized = true;
    esp_sntp_set_time_sync_notification_cb(noteSntpSynchronization);
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    startMdns();

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

    Serial.printf("Painel: http://%s\n", WiFi.localIP().toString().c_str());

    if (mdnsAvailable) {
        Serial.printf("Painel mDNS: http://%s.local/\n", LOCAL_HOSTNAME);
    }

    runHealthCheck();
}

void processPendingEmails()
{
    if (!ENABLE_EMAIL_ALERTS) {
        pendingIncidentEmail = false;
        pendingRecoveryEmail = false;
        return;
    }

    if (millis() - lastEmailAttemptAtMs < EMAIL_RETRY_INTERVAL_MS && lastEmailAttemptAtMs != 0) {
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
    uint16_t actionContextId = pendingAdminActionContextId;
    pendingAdminAction = AdminAction::None;
    pendingAdminActionContextId = 0;
    activeAdminAction = action;

    if (actionContextId == 0) {
        actionContextId = allocateLogContextId();
    }

    const unsigned long actionStartedAtMs = millis();
    bool actionSucceeded = true;

    switch (action) {
        case AdminAction::HealthCheck:
            lastAdminActionResult = currentTimestamp() + " - verificacao manual iniciada";
            runHealthCheck(true, actionContextId);
            lastAdminActionResult = currentTimestamp() + " - verificacao manual concluida: " + lastEvent;
            break;

        case AdminAction::Rediscover: {
            CameraIoGuard cameraIoGuard;

            if (!cameraIoGuard.acquired()) {
                lastAdminActionResult = currentTimestamp() + " - redescoberta nao executada: canal da camera ocupado";
                actionSucceeded = false;
                break;
            }

            setState(CameraState::Discovering, "Redescoberta manual solicitada pelo painel");
            const bool found = discoverOnvifCamera(true, actionContextId);
            actionSucceeded = found;

            if (found) {
                resolveCameraOnvifEndpoint(actionContextId);
                invalidateSnapshotTarget();
                invalidateOnvifServiceCatalog();
                resetSnapshotProfileFailover();
                lastSnapshotResolveAttemptAtMs = 0;
                resolveOnvifServiceCatalog(false, actionContextId);
                runHealthCheck(true, actionContextId);
            }

            lastAdminActionResult = currentTimestamp() + (found ? " - camera redescoberta e verificada" : " - redescoberta concluida sem camera compativel");
            break;
        }

        case AdminAction::GotoPtzPreset: {
            const String presetToken = pendingPtzPresetToken;
            pendingPtzPresetToken = String();
            const bool accepted = requestOnvifPtzPreset(presetToken, actionContextId);
            actionSucceeded = accepted;
            lastAdminActionResult = onvifPtz.lastCommandResult;

            if (!accepted && lastAdminActionResult.isEmpty()) {
                lastAdminActionResult = currentTimestamp() + " - movimento PTZ nao executado";
            }
            break;
        }

        case AdminAction::CameraReboot: {
            const bool accepted = requestCameraRebootOnvif(true, actionContextId);
            actionSucceeded = accepted;
            lastAdminActionResult = currentTimestamp() + (accepted ? " - reboot ONVIF manual aceito" : " - reboot ONVIF manual nao executado: ") + lastCameraRebootResult;
            break;
        }

        case AdminAction::ResetRebootLimiter:
            lastCameraRebootAttemptAtMs = 0;
            cameraRebootWindowStartedAtMs = 0;
            cameraRebootAttemptsInWindow = 0;

            for (uint8_t index = 0; index < MAX_ONVIF_REBOOT_ATTEMPTS_MAX; index++) {
                cameraRebootAcceptedAtMs[index] = 0;
            }

            lastCameraRebootResult = currentTimestamp() + " - limitador de reboots reiniciado";
            lastAdminActionResult = lastCameraRebootResult;
            break;

        case AdminAction::RestartEsp32:
            espRestartRequestedAtMs = millis();
            lastAdminActionResult = currentTimestamp() + " - reinicializacao do ESP32 agendada";
            break;

        case AdminAction::None:
            actionSucceeded = false;
            break;
    }

    PersistentLog::record(PersistentLog::Event::AdminAction, actionSucceeded ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, adminActionName(action), camera.ip, camera.onvifPort, 0, millis() - actionStartedAtMs, static_cast<uint32_t>(action), PersistentLog::FlagManual, actionSucceeded ? PersistentLog::Level::Info : PersistentLog::Level::Warning, true, actionContextId);
    activeAdminAction = AdminAction::None;
}

void maintainWiFi()
{
    static unsigned long lastReconnectAttemptAtMs = 0;

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiWasConnected) {
            wifiWasConnected = true;
            resetCameraCheckSequences("Wi-Fi restabelecido; aguardando nova verificacao");
            lastHealthCheckAtMs = 0;
            Serial.printf("Wi-Fi restabelecido. IP: %s\n", WiFi.localIP().toString().c_str());
            PersistentLog::record(PersistentLog::Event::WifiReconnect, PersistentLog::Outcome::Success, "station", WiFi.gatewayIP(), 0, static_cast<int>(WiFi.status()), 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
        }

        return;
    }

    if (wifiWasConnected) {
        wifiWasConnected = false;
        resetCameraSnapshotHttpSession();
        resetCameraCheckSequences("ESP32 perdeu a conexao Wi-Fi");
        setState(CameraState::Degraded, "ESP32 perdeu a conexao Wi-Fi");
        PersistentLog::record(PersistentLog::Event::WifiDisconnect, PersistentLog::Outcome::Failure, "station", IPAddress(), 0, static_cast<int>(WiFi.status()), 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Error, true);
    }

    if (millis() - lastReconnectAttemptAtMs < 10000UL) {
        return;
    }

    lastReconnectAttemptAtMs = millis();
    Serial.println("Tentando reconectar o ESP32 ao Wi-Fi...");
    PersistentLog::record(PersistentLog::Event::WifiReconnect, PersistentLog::Outcome::Started, "station", IPAddress(), 0, static_cast<int>(WiFi.status()), 0, 0, PersistentLog::FlagScheduled);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup()
{
    Serial.begin(115200);

    if (!PersistentLog::begin()) {
        Serial.println("Particao persistente de logs indisponivel.");
    }

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
    PersistentLog::record(PersistentLog::Event::SystemBoot, PersistentLog::Outcome::Success, resetReasonName(esp_reset_reason()), IPAddress(), 0, static_cast<int>(esp_reset_reason()), 0, ESP.getFreeHeap(), PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
    PersistentLog::record(PersistentLog::Event::RuntimeContext, PersistentLog::Outcome::Success, FIRMWARE_VERSION, IPAddress(), 0, RUNTIME_SETTINGS_SCHEMA_VERSION, 0, RUNTIME_SETTINGS_DEFAULTS_REVISION, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
    recordRuntimeSettingsContext(settingsLoadedFromNvs ? "nvs" : "factory", PersistentLog::FlagScheduled);

    if (!connectToWiFi()) {
        setState(CameraState::Degraded, "ESP32 sem Wi-Fi");
    }

    initializeConnectedServices();
}

void loop()
{
    PersistentLog::maintain();
    updateCameraAlertLed();
    maintainWiFi();
    initializeConnectedServices();

    if (connectedServicesInitialized) {
        httpServer.handleClient();
        maintainCameraSnapshotHttpSession();
        refreshOnvifDeviceMetadata();
        maintainCameraClock();
        maintainOnvifPtzPresets();
    }
    processPendingEmails();
    processPendingAdminAction();

    const SntpReferenceSnapshot sntpReference = snapshotSntpReference();

    if (sntpReference.synchronized && sntpReference.generation != loggedSntpSynchronizationGeneration) {
        loggedSntpSynchronizationGeneration = sntpReference.generation;
        PersistentLog::record(PersistentLog::Event::SntpSync, PersistentLog::Outcome::Success, "clock_synchronized", IPAddress(), 123, 0, 0, sntpReference.generation, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
    }

    if (espRestartRequestedAtMs != 0 && millis() - espRestartRequestedAtMs >= ADMIN_RESTART_DELAY_MS) {
        Serial.println("Reiniciando o ESP32 por solicitacao administrativa...");
        PersistentLog::record(PersistentLog::Event::SystemRestart, PersistentLog::Outcome::Started, "admin_request", IPAddress(), 0, 0, millis() - espRestartRequestedAtMs, 0, PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
        PersistentLog::flush();
        delay(50);
        ESP.restart();
    }

    if (millis() - lastHealthCheckAtMs >= HEALTH_CHECK_INTERVAL_MS) {
        runHealthCheck();
    }

    updateCameraAlertLed();
    delay(2);
}
}
