#include "firmware_internal.h"
#include <esp_attr.h>
#include <nvs_flash.h>

namespace CameraWatchdog {

constexpr unsigned long ADMIN_RESTART_DELAY_MS = 1200UL;
constexpr uint32_t FACTORY_RESET_FAILURE_MAGIC = 0x46525354UL;

struct FactoryResetFailureMarker {
    uint32_t magic;
    uint32_t invertedMagic;
    int32_t eraseError;
};

RTC_NOINIT_ATTR volatile FactoryResetFailureMarker factoryResetFailureMarker;

bool cameraControlResponded(CameraContext& ctx, uint16_t contextId = 0)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();

    if (!ctx.camera.valid) {
        ctx.lastOnvifProbe = ServiceProbe{};
        ctx.lastRtspProbe = ServiceProbe{};
        ctx.lastRtspProbeAttempted = false;
        return false;
    }

    ctx.lastOnvifProbe = probeOnvif(ctx, ctx.camera, logContextId);

    if (ctx.lastOnvifProbe.protocolResponded) {
        ctx.preferredFailureStreak = 0;
        ctx.lastRtspProbe = ServiceProbe{};
        ctx.lastRtspProbeAttempted = false;
        return true;
    }

    if (ctx.preferredFailureStreak < 255)
        ctx.preferredFailureStreak++;
    if (ctx.preferredFailureStreak >= 2 && switchToAlternateEndpoint(ctx)) {
        ctx.lastOnvifProbe = probeOnvif(ctx, ctx.camera, logContextId);
        if (ctx.lastOnvifProbe.protocolResponded)
            return true;
    }

    handleHttpClient();
    ctx.lastRtspProbe = probeRtsp(ctx, ctx.camera.ip, ctx.camera.rtspPort, logContextId);
    ctx.lastRtspProbeAttempted = true;

    return ctx.lastRtspProbe.protocolResponded;
}

void resetCameraCheckSequences(CameraContext& ctx, const char* snapshotDetail)
{
    ctx.consecutiveFailures = 0;
    ctx.consecutiveSuccesses = 0;
    ctx.consecutiveControlFailures = 0;
    ctx.consecutiveInconclusiveChecks = 0;
    ctx.consecutiveMediaServiceFailures = 0;
    ctx.snapshotTargetFailureStreak = 0;
    resetSnapshotProfileFailover(ctx);
    ctx.lastOnvifProbe = ServiceProbe{};
    ctx.lastRtspProbe = ServiceProbe{};
    ctx.lastRtspProbeAttempted = false;
    ctx.lastSnapshotProbe = SnapshotProbe{};
    ctx.lastSnapshotProbe.detail = snapshotDetail;
    ctx.lastBrowserSnapshotDeliveredAtMs = 0;
    ctx.lastBrowserSnapshotBytes = 0;
    ctx.lastBrowserSnapshotLatencyMs = 0;
    ctx.lastHealthEvidenceSource = HealthEvidenceSource::None;
    ctx.lastHealthEvidenceAtMs = 0;
}

bool browserSnapshotRecentlyDelivered(CameraContext& ctx)
{
    return ctx.lastBrowserSnapshotDeliveredAtMs != 0 && millis() - ctx.lastBrowserSnapshotDeliveredAtMs <= ctx.settings.healthCheckIntervalMs;
}

void noteInconclusiveHealthCheck(CameraContext& ctx, const String& event)
{
    ctx.consecutiveSuccesses = 0;

    if (ctx.cameraState == CameraState::Online && ctx.activeIncident == IncidentKind::None) {
        ctx.lastEvent = currentTimestamp() + " - " + event;
        Serial.printf("CHECK DEFERRED: %s\n", event.c_str());
        return;
    }

    setState(ctx, CameraState::Degraded, event);
}

void recordSuccessfulHealthCheck(CameraContext& ctx, HealthEvidenceSource evidenceSource, const char* confirmationEvent, const char* healthyEvent, const char* recoveredEvent)
{
    ctx.successfulHealthChecks++;
    ctx.lastHealthEvidenceSource = evidenceSource;
    ctx.lastHealthEvidenceAtMs = millis();
    ctx.consecutiveFailures = 0;
    ctx.consecutiveControlFailures = 0;
    ctx.consecutiveInconclusiveChecks = 0;
    ctx.consecutiveMediaServiceFailures = 0;

    if (ctx.consecutiveSuccesses < 255) {
        ctx.consecutiveSuccesses++;
    }

    ctx.cameraRebootGraceStartedAtMs = 0;

    if (ctx.activeIncident != IncidentKind::None && ctx.consecutiveSuccesses < ctx.settings.successesBeforeRecovery) {
        setState(ctx, CameraState::Degraded, confirmationEvent);
        return;
    }

    const bool recovered = ctx.activeIncident != IncidentKind::None;
    resolveActiveIncident(ctx);
    setState(ctx, CameraState::Online, recovered ? recoveredEvent : healthyEvent);
}

struct HealthCheckScheduleGuard {
    HealthCheckScheduleGuard(CameraContext& cameraContext, bool manualRequest, uint16_t logContextId) : ctx(cameraContext), startedAtMs(millis()), successfulBefore(ctx.successfulHealthChecks), failedBefore(ctx.failedHealthChecks), inconclusiveBefore(ctx.inconclusiveHealthChecks), manual(manualRequest), contextId(logContextId)
    {
        healthCheckInProgress = true;
    }

    ~HealthCheckScheduleGuard()
    {
        healthCheckInProgress = false;
        ctx.lastHealthCheckAtMs = millis();

        PersistentLog::Outcome outcome = PersistentLog::Outcome::Skipped;
        PersistentLog::Level level = PersistentLog::Level::Warning;
        const char* detail = "no_conclusive_result";

        if (ctx.successfulHealthChecks > successfulBefore) {
            outcome = PersistentLog::Outcome::Success;
            level = PersistentLog::Level::Info;
            detail = healthEvidenceSourceName(ctx.lastHealthEvidenceSource);
        } else if (ctx.failedHealthChecks > failedBefore) {
            outcome = PersistentLog::Outcome::Failure;
            level = PersistentLog::Level::Error;
            detail = stateName(ctx.cameraState);
        } else if (ctx.inconclusiveHealthChecks > inconclusiveBefore) {
            detail = "inconclusive";
        } else if (cameraRebootGraceActive(ctx)) {
            detail = "camera_reboot_grace";
        }

        PersistentLog::record(PersistentLog::Event::HealthCheck, outcome, detail, ctx.camera.ip, ctx.camera.onvifPort, ctx.lastSnapshotProbe.statusCode, millis() - startedAtMs, ctx.totalHealthChecks, manual ? PersistentLog::FlagManual : PersistentLog::FlagScheduled, level, false, contextId, ctx.id);
    }

    CameraContext& ctx;
    unsigned long startedAtMs;
    uint32_t successfulBefore;
    uint32_t failedBefore;
    uint32_t inconclusiveBefore;
    bool manual;
    uint16_t contextId;
};

void runHealthCheck(CameraContext& ctx, bool manualRequest, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    HealthCheckScheduleGuard scheduleGuard(ctx, manualRequest, logContextId);

    if (ctx.cameraRebootGraceStartedAtMs != 0 && !cameraRebootGraceActive(ctx)) {
        ctx.cameraRebootGraceStartedAtMs = 0;
    }

    if (WiFi.status() != WL_CONNECTED) {
        ctx.totalHealthChecks++;
        ctx.failedHealthChecks++;
        resetCameraCheckSequences(ctx, "ESP32 without Wi-Fi connection");
        setState(ctx, CameraState::Degraded, "ESP32 lost the Wi-Fi connection");
        return;
    }

    if (!ctx.camera.valid) {
        ctx.totalHealthChecks++;
        ctx.inconclusiveHealthChecks++;
        discoveryRequested = true;
        setState(ctx, CameraState::Discovering, "Address suspended; waiting for a new identification");
        return;
    }

    if (cameraRebootGraceActive(ctx)) {
        setState(ctx, CameraState::Recovering, String("Waiting for the camera to restart; about ") + String(cameraRebootGraceRemainingMs(ctx) / 1000UL) + "s left");
        return;
    }

    float liveFramesPerSecond = 0;
    if (liveStreamDeliveredRecently(ctx.id, ctx.settings.healthCheckIntervalMs, &liveFramesPerSecond)) {
        ctx.totalHealthChecks++;
        ctx.previewHealthEvidenceUses++;
        ctx.lastSnapshotProbe = SnapshotProbe{};
        ctx.lastSnapshotProbe.attempted = true;
        ctx.lastSnapshotProbe.conclusive = true;
        ctx.lastSnapshotProbe.targetResolved = ctx.snapshotTarget.valid;
        ctx.lastSnapshotProbe.httpResponded = true;
        ctx.lastSnapshotProbe.jpegValid = true;
        ctx.lastSnapshotProbe.statusCode = 200;
        ctx.lastSnapshotProbe.latencyMs = 0;
        ctx.lastSnapshotProbe.bytesRead = 0;
        ctx.lastSnapshotProbe.detail = String("Live H.264 video received from the camera (") + liveFramesPerSecond + " frames/s)";
        recordSuccessfulHealthCheck(ctx, HealthEvidenceSource::LiveStream, "Live video returned; confirming stability", "Camera validated by live video", "Camera recovered and live video validated");
        return;
    }

    if (browserSnapshotRecentlyDelivered(ctx)) {
        ctx.totalHealthChecks++;
        ctx.previewHealthEvidenceUses++;
        ctx.lastSnapshotProbe = SnapshotProbe{};
        ctx.lastSnapshotProbe.attempted = true;
        ctx.lastSnapshotProbe.conclusive = true;
        ctx.lastSnapshotProbe.targetResolved = ctx.snapshotTarget.valid;
        ctx.lastSnapshotProbe.httpResponded = true;
        ctx.lastSnapshotProbe.jpegValid = true;
        ctx.lastSnapshotProbe.statusCode = 200;
        ctx.lastSnapshotProbe.latencyMs = ctx.lastBrowserSnapshotLatencyMs;
        ctx.lastSnapshotProbe.bytesRead = ctx.lastBrowserSnapshotBytes;
        ctx.lastSnapshotProbe.detail = "JPEG validated by a recent Preview";
        recordSuccessfulHealthCheck(ctx, HealthEvidenceSource::PreviewJpeg, "Preview returned; confirming stability", "Camera validated by a recent Preview", "Camera recovered and JPEG validated by Preview");
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        ctx.totalHealthChecks++;
        ctx.inconclusiveHealthChecks++;
        noteInconclusiveHealthCheck(ctx, "Camera channel busy; check deferred");
        return;
    }

    ctx.totalHealthChecks++;

    if (!ctx.camera.valid)
        discoveryRequested = true;

    const bool controlAvailable = cameraControlResponded(ctx, logContextId);
    const uint8_t nextControlFailure = ctx.consecutiveControlFailures < 255 ? ctx.consecutiveControlFailures + 1 : ctx.consecutiveControlFailures;
    if (!controlAvailable && nextControlFailure >= ctx.settings.failuresBeforeRediscovery)
        discoveryRequested = true;
    if (!controlAvailable && nextControlFailure >= ctx.settings.failuresBeforeOffline)
        subnetScanRequested = true;

    const bool videoAvailable = probeCameraSnapshotJpeg(ctx, logContextId);

    if (videoAvailable) {
        Serial.printf("CAMERA OK: %s | JPEG=%u bytes %lums | " "ONVIF tcp=%d http=%d code=%d | " "RTSP attempted=%d tcp=%d protocol=%d code=%d\n", ctx.camera.ip.toString().c_str(), static_cast<unsigned>(ctx.lastSnapshotProbe.bytesRead), static_cast<unsigned long>(ctx.lastSnapshotProbe.latencyMs), ctx.lastOnvifProbe.tcpConnected, ctx.lastOnvifProbe.protocolResponded, ctx.lastOnvifProbe.statusCode, ctx.lastRtspProbeAttempted, ctx.lastRtspProbe.tcpConnected, ctx.lastRtspProbe.protocolResponded, ctx.lastRtspProbe.statusCode);

        recordSuccessfulHealthCheck(ctx, HealthEvidenceSource::SnapshotJpeg, "Image returned; confirming stability", "Camera and image channel responded to the test", "Camera recovered and JPEG validated");
        return;
    }

    const bool mediaServiceTransportFailure = !ctx.lastSnapshotProbe.conclusive && controlAvailable && ctx.lastSnapshotProbe.requestAttempted && ctx.lastSnapshotProbe.resolutionAttempted && ctx.lastSnapshotProbe.resolutionTransportFailure && ctx.lastOnvifProbe.protocolResponded;

    if (!ctx.lastSnapshotProbe.conclusive && controlAvailable) {

        if (mediaServiceTransportFailure) {
            if (ctx.consecutiveMediaServiceFailures < 255) {
                ctx.consecutiveMediaServiceFailures++;
            }
        } else if (ctx.lastSnapshotProbe.requestAttempted) {
            ctx.consecutiveMediaServiceFailures = 0;
        }

        if (mediaServiceTransportFailure && ctx.consecutiveMediaServiceFailures >= ctx.settings.videoFailuresBeforeReboot) {
            ctx.lastSnapshotProbe.conclusive = true;
            ctx.lastSnapshotProbe.detail = String("ONVIF media service failed repeatedly: ") + ctx.lastSnapshotProbe.detail;
            resetSnapshotProfileFailover(ctx);
        }
    }

    if (!ctx.lastSnapshotProbe.conclusive && controlAvailable) {
        ctx.consecutiveControlFailures = 0;

        if (ctx.lastSnapshotProbe.requestAttempted && ctx.consecutiveInconclusiveChecks < 255) {
            ctx.consecutiveInconclusiveChecks++;
        }

        ctx.inconclusiveHealthChecks++;

        noteInconclusiveHealthCheck(ctx, String("Inconclusive snapshot monitor: ") + ctx.lastSnapshotProbe.detail);
        return;
    }

    ctx.consecutiveInconclusiveChecks = 0;

    if (!ctx.lastSnapshotProbe.resolutionAttempted) {
        ctx.consecutiveMediaServiceFailures = 0;
    }

    ctx.consecutiveSuccesses = 0;
    ctx.failedHealthChecks++;

    if (ctx.lastSnapshotProbe.conclusive && ctx.consecutiveFailures < 255) {
        if (mediaServiceTransportFailure && ctx.consecutiveMediaServiceFailures > ctx.consecutiveFailures) {
            ctx.consecutiveFailures = ctx.consecutiveMediaServiceFailures;
        } else {
            ctx.consecutiveFailures++;
        }
    }

    if (controlAvailable) {
        ctx.consecutiveControlFailures = 0;

        if (millis() - ctx.registeredAtMs < ctx.settings.startupGraceMs) {
            setState(ctx, CameraState::Degraded, String("Camera in startup grace period; image failed: ") + ctx.lastSnapshotProbe.detail);
            return;
        }

        Serial.printf("SNAPSHOT FAILED: attempts=%u | IP=%s | HTTP=%d | %s\n", ctx.consecutiveFailures, ctx.camera.valid ? ctx.camera.ip.toString().c_str() : "unknown", ctx.lastSnapshotProbe.statusCode, ctx.lastSnapshotProbe.detail.c_str());

        if (ctx.consecutiveFailures >= ctx.settings.videoFailuresBeforeAlert) {
            beginOrEscalateIncident(ctx, IncidentKind::SnapshotUnavailable);
            queueIncidentAlert(ctx);
        }

        if (ctx.consecutiveFailures >= ctx.settings.videoFailuresBeforeReboot) {
            const bool rebootEligible = ctx.lastOnvifProbe.protocolResponded && snapshotFailureAllowsAutomaticReboot(ctx);

            if (rebootEligible && requestCameraRebootOnvif(ctx, false, logContextId)) {
                return;
            }

            if (ctx.settings.enableOnvifAutoReboot && !rebootEligible) {
                ctx.lastCameraRebootResult = currentTimestamp() + " - not requested: ";

                if (!ctx.lastOnvifProbe.protocolResponded) {
                    ctx.lastCameraRebootResult += "ONVIF service not responding, so it cannot receive SystemReboot";
                } else if (ctx.snapshotProfileFailoverPending) {
                    ctx.lastCameraRebootResult += "alternate snapshot profile still being tested";
                } else {
                    ctx.lastCameraRebootResult += "the failure points to authentication, an unsupported feature, or configuration";
                }
            }
        }

        setState(ctx, CameraState::Degraded, String("Camera responds, but the snapshot failed: ") + ctx.lastSnapshotProbe.detail);
        return;
    }

    if (ctx.consecutiveControlFailures < 255) {
        ctx.consecutiveControlFailures++;
    }

    Serial.printf("CAMERA WITHOUT CONTROL AND WITHOUT IMAGE: video=%u control=%u | IP=%s\n", ctx.consecutiveFailures, ctx.consecutiveControlFailures, ctx.camera.valid ? ctx.camera.ip.toString().c_str() : "unknown");

    if (ctx.consecutiveControlFailures < ctx.settings.failuresBeforeOffline || millis() - ctx.registeredAtMs < ctx.settings.startupGraceMs) {
        setState(ctx, CameraState::Degraded, "Camera did not respond; confirming unavailability");
        return;
    }

    beginOrEscalateIncident(ctx, IncidentKind::CameraOffline);
    queueIncidentAlert(ctx);
    setState(ctx, CameraState::Offline, "Camera confirmed unavailable and without image");
}

bool connectToWiFi()
{
    const unsigned long connectStartedAtMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(globalSettings.localHostname);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);

    WifiCredential candidates[WIFI_CREDENTIAL_CANDIDATES_MAX];
    const size_t candidateCount = wifiCredentialCandidates(candidates);
    if (candidateCount == 0) {
        Serial.println("No Wi-Fi network configured. The ESP32 setup network will open.");
        PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Skipped, "no_credentials", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true);
        return false;
    }

    for (size_t index = 0; index < candidateCount && WiFi.status() != WL_CONNECTED; index++) {
        const WifiCredential& candidate = candidates[index];
        Serial.printf("Connecting to network \"%s\"", candidate.ssid);
        WiFi.begin(candidate.ssid, strlen(candidate.password) > 0 ? candidate.password : nullptr);

        const unsigned long startedAt = millis();

        while (WiFi.status() != WL_CONNECTED && millis() - startedAt < globalSettings.wifiConnectTimeoutMs) {
            updateCameraAlertLed();
            Serial.print('.');
            delay(500);
        }

        Serial.println();
        if (WiFi.status() != WL_CONNECTED)
            WiFi.disconnect(false);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Failed to connect to Wi-Fi.");
        PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Failure, "station", IPAddress(), 0, static_cast<int>(WiFi.status()), millis() - connectStartedAtMs, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Error, true);
        return false;
    }

    Serial.printf("Wi-Fi connected to \"%s\". IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Subnet mask: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    wifiWasConnected = true;

    return true;
}

bool startMdns()
{
    mdnsAvailable = MDNS.begin(globalSettings.localHostname);

    if (!mdnsAvailable) {
        Serial.println("Failed to start mDNS. The dashboard is available by IP.");
        return false;
    }

    MDNS.addService("http", "tcp", HTTP_SERVER_PORT);
    Serial.printf("mDNS: http://%s.local/\n", globalSettings.localHostname);
    return true;
}

void initializeConnectedServices()
{
    if (connectedServicesInitialized || WiFi.status() != WL_CONNECTED)
        return;
    connectedServicesInitialized = true;
    esp_sntp_set_time_sync_notification_cb(noteSntpSynchronization);
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    startMdns();
    configureHttpServer();
    liveStreamBegin();
    discoverOnvifCameras();
    if (cameraCount == 0)
        subnetScanRequested = true;
    Serial.printf("Dashboard: http://%s | cameras=%u\n", WiFi.localIP().toString().c_str(), static_cast<unsigned>(cameraCount));
}

void processPendingEmails()
{
    if (WiFi.status() != WL_CONNECTED || anyCameraHealthCheckDue() || currentAdminAction() != AdminAction::None)
        return;
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        if (!ctx->settings.enableEmailAlerts)
            continue;
        if (ctx->lastEmailAttemptAtMs != 0 && millis() - ctx->lastEmailAttemptAtMs < globalSettings.emailRetryIntervalMs)
            continue;
        if (ctx->pendingIncidentEmail) {
            sendIncidentAlert(*ctx);
            return;
        }
        if (ctx->pendingRecoveryEmail) {
            sendRecoveryAlert(*ctx);
            return;
        }
        if (ctx->pendingIpChangeEmail) {
            if (!ctx->settings.emailOnIpChange || sendIpChangeAlert(*ctx, ctx->previousCameraIp, ctx->camera.ip.toString()))
                ctx->pendingIpChangeEmail = false;
            return;
        }
    }
}

void processPendingAdminAction()
{
    if (activeAdminAction == AdminAction::RebootAllCameras) {
        CameraContext* next = nullptr;
        for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
            if (ctx->id >= batchNextCameraId && ctx->id <= batchLastCameraId) {
                next = ctx;
                break;
            }
        }
        if (next == nullptr) {
            lastAdminActionResult = String("Bulk restart complete: ") + batchAccepted + " accepted; " + batchFailed + " failed or were blocked";
            activeAdminAction = AdminAction::None;
            activeAdminCameraId = 0;
            return;
        }
        activeAdminCameraId = next->id;
        const bool accepted = requestCameraRebootOnvif(*next, true, pendingAdminActionContextId);
        batchCompleted++;
        if (accepted)
            batchAccepted++;
        else
            batchFailed++;
        batchNextCameraId = next->id + 1;
        lastAdminActionResult = String("Bulk restart: ") + batchCompleted + " processed; last camera: " + next->settings.cameraName + (accepted ? " - command accepted" : " - failed or blocked");
        return;
    }
    if (pendingAdminAction == AdminAction::None)
        return;
    const AdminAction action = pendingAdminAction;
    const uint16_t contextId = pendingAdminActionContextId != 0 ? pendingAdminActionContextId : allocateLogContextId();
    CameraContext* ctx = findCamera(pendingAdminCameraId);
    activeAdminCameraId = pendingAdminCameraId;
    pendingAdminAction = AdminAction::None;
    pendingAdminCameraId = 0;
    activeAdminAction = action;
    const unsigned long startedAtMs = millis();
    bool succeeded = false;
    switch (action) {
        case AdminAction::HealthCheck:
            if (ctx != nullptr) {
                runHealthCheck(*ctx, true, contextId);
                succeeded = true;
                lastAdminActionResult = String(ctx->settings.cameraName) + ": " + ctx->lastEvent;
            }
            break;
        case AdminAction::Rediscover:
            succeeded = discoverOnvifCameras(true, contextId);
            lastAdminActionResult = lastDiscoveryResult + "; " + lastRegistryResult;
            break;
        case AdminAction::GotoPtzPreset:
            if (ctx != nullptr) {
                succeeded = requestOnvifPtzPreset(*ctx, pendingPtzPresetToken, contextId);
                lastAdminActionResult = String(ctx->settings.cameraName) + ": " + ctx->onvifPtz.lastCommandResult;
            }
            pendingPtzPresetToken = String();
            break;
        case AdminAction::CameraReboot:
            if (ctx != nullptr) {
                succeeded = requestCameraRebootOnvif(*ctx, true, contextId);
                lastAdminActionResult = String(ctx->settings.cameraName) + ": " + ctx->lastCameraRebootResult;
            }
            break;
        case AdminAction::RebootAllCameras:
            batchNextCameraId = 1;
            batchLastCameraId = 0;
            batchCompleted = batchAccepted = batchFailed = 0;
            for (CameraContext* item = cameras.get(); item != nullptr; item = item->next.get())
                batchLastCameraId = item->id;
            pendingAdminActionContextId = contextId;
            lastAdminActionResult = "Bulk restart in progress";
            return;
        case AdminAction::ResetRebootLimiter:
            if (ctx != nullptr) {
                const unsigned long previousAttempt = ctx->lastCameraRebootAttemptAtMs;
                const unsigned long previousWindow = ctx->cameraRebootWindowStartedAtMs;
                const uint8_t previousCount = ctx->cameraRebootAttemptsInWindow;
                unsigned long previousAccepted[MAX_ONVIF_REBOOT_ATTEMPTS_MAX];
                memcpy(previousAccepted, ctx->cameraRebootAcceptedAtMs, sizeof(previousAccepted));
                ctx->lastCameraRebootAttemptAtMs = 0;
                ctx->cameraRebootWindowStartedAtMs = 0;
                ctx->cameraRebootAttemptsInWindow = 0;
                memset(ctx->cameraRebootAcceptedAtMs, 0, sizeof(ctx->cameraRebootAcceptedAtMs));
                if (!persistRebootLimiter(*ctx)) {
                    ctx->lastCameraRebootAttemptAtMs = previousAttempt;
                    ctx->cameraRebootWindowStartedAtMs = previousWindow;
                    ctx->cameraRebootAttemptsInWindow = previousCount;
                    memcpy(ctx->cameraRebootAcceptedAtMs, previousAccepted, sizeof(previousAccepted));
                    lastAdminActionResult = "Failed to persist the limiter reset";
                    break;
                }
                ctx->lastCameraRebootResult = "Reboot limiter reset";
                lastAdminActionResult = String(ctx->settings.cameraName) + ": " + ctx->lastCameraRebootResult;
                succeeded = true;
            }
            break;
        case AdminAction::RestartEsp32:
            espRestartRequestedAtMs = millis();
            lastAdminActionResult = "ESP32 restart scheduled";
            succeeded = true;
            break;
        case AdminAction::None:
            break;
    }
    PersistentLog::record(PersistentLog::Event::AdminAction, succeeded ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, adminActionName(action), ctx != nullptr ? ctx->camera.ip : IPAddress(), ctx != nullptr ? ctx->camera.onvifPort : 0, 0, millis() - startedAtMs, static_cast<uint32_t>(action), PersistentLog::FlagManual, succeeded ? PersistentLog::Level::Info : PersistentLog::Level::Warning, true, contextId, ctx != nullptr ? ctx->id : 0);
    activeAdminAction = AdminAction::None;
    activeAdminCameraId = 0;
    pendingAdminActionContextId = 0;
}

void maintainWiFi()
{
    static unsigned long lastReconnectAttemptAtMs = 0;
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiWasConnected) {
            wifiWasConnected = true;
            for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
                resetCameraCheckSequences(*ctx, "Wi-Fi restored; waiting for a new check");
                ctx->lastHealthCheckAtMs = 0;
            }
        }
        return;
    }
    if (wifiWasConnected) {
        wifiWasConnected = false;
        for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
            resetCameraSnapshotHttpSession(*ctx);
            resetCameraCheckSequences(*ctx, "ESP32 lost the Wi-Fi connection");
            ctx->cameraState = CameraState::Degraded;
            ctx->lastStateChangeAtMs = millis();
            ctx->lastEvent = "ESP32 without Wi-Fi connection; monitoring suspended";
        }
        PersistentLog::record(PersistentLog::Event::WifiDisconnect, PersistentLog::Outcome::Failure, "station", IPAddress(), 0, static_cast<int>(WiFi.status()), 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Error, true);
    }
    if (wifiSetupHoldsStation())
        return;
    const unsigned long retryIntervalMs = wifiSetupAccessPointActive() ? 45000UL : 10000UL;
    if (millis() - lastReconnectAttemptAtMs < retryIntervalMs)
        return;
    lastReconnectAttemptAtMs = millis();
    WifiCredential candidates[WIFI_CREDENTIAL_CANDIDATES_MAX];
    const size_t candidateCount = wifiCredentialCandidates(candidates);
    if (candidateCount == 0)
        return;
    static size_t nextCandidate = 0;
    const WifiCredential& candidate = candidates[nextCandidate++ % candidateCount];
    WiFi.disconnect();
    WiFi.begin(candidate.ssid, strlen(candidate.password) > 0 ? candidate.password : nullptr);
}

bool cameraHealthCheckDue(const CameraContext& ctx)
{
    return WiFi.status() == WL_CONNECTED && ctx.camera.valid && (ctx.lastHealthCheckAtMs == 0 || millis() - ctx.lastHealthCheckAtMs >= ctx.settings.healthCheckIntervalMs);
}

bool anyCameraHealthCheckDue()
{
    for (const CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        if (cameraHealthCheckDue(*ctx))
            return true;
    }
    return false;
}

void setup()
{
    Serial.begin(115200);

    if (factoryResetFailureMarker.magic == FACTORY_RESET_FAILURE_MAGIC && factoryResetFailureMarker.invertedMagic == ~FACTORY_RESET_FAILURE_MAGIC) {
        const esp_err_t eraseError = static_cast<esp_err_t>(factoryResetFailureMarker.eraseError);
        factoryResetFailureMarker.magic = 0;
        factoryResetFailureMarker.invertedMagic = 0;
        factoryResetFailed = true;
        lastAdminActionResult = String("Failed to erase NVS: ") + esp_err_to_name(eraseError) + "; the data may have been partially erased";
        Serial.println(lastAdminActionResult);
    }

    if (!PersistentLog::begin()) {
        Serial.println("Persistent logs partition unavailable.");
    }

    settingsWriteToken = makeSettingsWriteToken();
    loadRuntimeSettings();
    configureCameraAlertLed();
    updateCameraAlertLed();
    delay(800);

    Serial.println();
    Serial.println("ESP32 Camera Watchdog");
    Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
    Serial.printf("Chip: %s rev. %u\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("Flash: %u MB\n", ESP.getFlashChipSize() / 1024U / 1024U);
    sketchSizeBytes = ESP.getSketchSize();
    releaseUnusedBluetoothMemory();
    Serial.printf("Initial free heap: %u bytes\n", ESP.getFreeHeap());

    wifiSetupBegin();
    if (!connectToWiFi()) {
        lastRegistryResult = "ESP32 without Wi-Fi; monitoring suspended";
    }

    initializeConnectedServices();
}

void loop()
{
    static uint32_t lastCheckedId = 0;
    static uint32_t lastMaintenanceId = 0;
    static uint8_t maintenanceStage = 0;
    static unsigned long lastIdentityReconcileAtMs = 0;
    if (factoryResetRequestedAtMs != 0) {
        if (millis() - factoryResetRequestedAtMs < ADMIN_RESTART_DELAY_MS) {
            delay(2);
            return;
        }

        factoryResetRequestedAtMs = 0;
        if (!PersistentLog::clear()) {
            factoryResetFailed = true;
            lastAdminActionResult = "Failed to clear the logs; configuration data preserved";
            Serial.println(lastAdminActionResult);
            PersistentLog::begin();
            return;
        }

        const esp_err_t eraseResult = nvs_flash_erase();
        if (eraseResult != ESP_OK) {
            lastAdminActionResult = String("Failed to erase NVS: ") + esp_err_to_name(eraseResult) + "; the data may have been partially erased";
            Serial.println(lastAdminActionResult);
            PersistentLog::record(PersistentLog::Event::AdminAction, PersistentLog::Outcome::Failure, "factory_reset", IPAddress(), 0, static_cast<int>(eraseResult), 0, 0, PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
            PersistentLog::flush();
            factoryResetFailureMarker.magic = 0;
            factoryResetFailureMarker.invertedMagic = ~FACTORY_RESET_FAILURE_MAGIC;
            factoryResetFailureMarker.eraseError = eraseResult;
            factoryResetFailureMarker.magic = FACTORY_RESET_FAILURE_MAGIC;
            ESP.restart();
            return;
        }

        ESP.restart();
        return;
    }

    liveStreamRecordPendingFailure();
    PersistentLog::maintain();
    firmwareUpdateMaintain();
    updateCameraAlertLed();
    wifiSetupLoop();
    maintainWiFi();
    initializeConnectedServices();
    if (connectedServicesInitialized || httpServerRunning)
        handleHttpClient();
    if (factoryResetRequestedAtMs != 0)
        return;
    if (espRestartRequestedAtMs != 0 && millis() - espRestartRequestedAtMs >= ADMIN_RESTART_DELAY_MS) {
        PersistentLog::record(PersistentLog::Event::SystemRestart, PersistentLog::Outcome::Started, "admin_request", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
        PersistentLog::flush();
        delay(50);
        ESP.restart();
    }
    if (firmwareUpdateMaintenanceActive()) {
        delay(2);
        return;
    }
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get())
        maintainCameraSnapshotHttpSession(*ctx);
    processPendingAdminAction();

    CameraContext* due = nullptr;
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        if (!cameraHealthCheckDue(*ctx))
            continue;
        if (due == nullptr)
            due = ctx;
        if (ctx->id > lastCheckedId) {
            due = ctx;
            break;
        }
    }
    if (due != nullptr) {
        lastCheckedId = due->id;
        runHealthCheck(*due);
    } else if (WiFi.status() == WL_CONNECTED && currentAdminAction() == AdminAction::None) {
        const unsigned long now = millis();
        const bool discoveryDue = lastRediscoveryAtMs == 0 || now - lastRediscoveryAtMs >= globalSettings.rediscoveryIntervalMs;
        if (discoveryDue && (discoveryRequested || now - lastRediscoveryAtMs >= globalSettings.subnetScanIntervalMs)) {
            discoverOnvifCameras();
        } else {
            CameraContext* maintenance = cameras.get();
            for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
                if (ctx->id > lastMaintenanceId) {
                    maintenance = ctx;
                    break;
                }
            }
            if (maintenance != nullptr) {
                lastMaintenanceId = maintenance->id;
                if (maintenanceStage == 0)
                    refreshOnvifDeviceMetadata(*maintenance);
                else if (maintenanceStage == 1)
                    maintainCameraClock(*maintenance);
                else
                    maintainOnvifPtzPresets(*maintenance);
                if (maintenance->next == nullptr)
                    maintenanceStage = (maintenanceStage + 1) % 3;
            }
            processPendingEmails();
            maintainDiscoveryIdentityCandidates();
            maintainSubnetScan();
            if (millis() - lastIdentityReconcileAtMs >= 30000UL) {
                lastIdentityReconcileAtMs = millis();
                reconcileIdentifiedCameras();
            }
        }
    }
    updateCameraAlertLed();
    delay(2);
}
}
