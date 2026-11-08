#include "firmware_internal.h"

namespace CameraWatchdog {

void appendLiveStreamJson(String& json);

const char* factoryResetStateName()
{
    if (factoryResetRequestedAtMs != 0)
        return "scheduled";
    return factoryResetFailed ? "failed" : "idle";
}

void appendOnvifEndpointJson(String& json, const char* name, const OnvifServiceEndpoint& endpoint, bool prependComma)
{
    if (prependComma) {
        json += ',';
    }

    json += '"';
    json += name;
    json += "\":{\"available\":";
    json += endpoint.available ? "true" : "false";
    json += ",\"validated\":";
    json += endpoint.validated ? "true" : "false";
    json += ",\"url\":\"";
    json += jsonEscape(endpoint.url);
    json += "\",\"version\":\"";
    json += endpoint.majorVersion;
    json += '.';
    json += endpoint.minorVersion;
    json += "\"}";
}

String buildStatusJson(CameraContext& ctx)
{
    refreshCameraRebootWindow(ctx);
    const uint32_t heapTotalBytes = ESP.getHeapSize();
    const uint32_t heapFreeBytes = ESP.getFreeHeap();
    const uint32_t heapMinimumFreeBytes = ESP.getMinFreeHeap();
    const uint32_t heapLargestBlockBytes = ESP.getMaxAllocHeap();
    const uint32_t psramTotalBytes = ESP.getPsramSize();
    const uint32_t psramFreeBytes = ESP.getFreePsram();
    const uint32_t psramMinimumFreeBytes = ESP.getMinFreePsram();
    const uint32_t psramLargestBlockBytes = ESP.getMaxAllocPsram();
    const SntpReferenceSnapshot sntpReference = snapshotSntpReference();
    const unsigned long now = millis();
    const bool sntpReferenceIsFresh = sntpReferenceFresh(sntpReference);

    String json;
    json.reserve(22000 + static_cast<size_t>(ctx.onvifPtz.presetCount) * 240);

    json += "{\"cameraId\":";
    json += ctx.id;
    json += ',';
    json += "\"firmwareVersion\":\"";
    json += FIRMWARE_VERSION;
    json += "\",\"state\":\"";
    json += stateName(ctx.cameraState);
    json += "\",\"lastEvent\":\"";
    json += jsonEscape(ctx.lastEvent);
    json += "\",\"uptimeSeconds\":";
    json += now / 1000UL;
    json += ",\"stateSinceSeconds\":";
    json += (now - ctx.lastStateChangeAtMs) / 1000UL;
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
        const esp_partition_t* runningPartition = esp_ota_get_running_partition();
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
    json += "\",\"hostname\":\"";
    json += globalSettings.localHostname;
    json += ".local\",\"localUrl\":\"http://";
    json += globalSettings.localHostname;
    json += ".local/\",\"mdnsAvailable\":";
    json += mdnsAvailable && WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"gateway\":\"";
    json += WiFi.gatewayIP().toString();
    json += "\",\"rssi\":";
    json += WiFi.RSSI();
    json += "},";

    json += "\"alertLed\":{\"configured\":";
    json += globalSettings.enableCameraAlertLed ? "true" : "false";
    json += ",\"blinking\":";
    json += cameraAlertLedBlinking ? "true" : "false";
    json += ",\"on\":";
    json += cameraAlertLedOn ? "true" : "false";
    json += ",\"reason\":\"";

    if (!globalSettings.enableCameraAlertLed) {
        json += "disabled";
    } else if (WiFi.status() != WL_CONNECTED) {
        json += "wifi_disconnected";
    } else if (ctx.activeIncident != IncidentKind::None) {
        json += incidentName(ctx.activeIncident);
    } else if (ctx.cameraState == CameraState::Offline) {
        json += "camera_offline";
    } else if (ctx.cameraState == CameraState::Recovering) {
        json += "camera_recovering";
    } else {
        json += "none";
    }

    json += "\"},";

    json += "\"camera\":{";
    json += "\"id\":";
    json += ctx.id;
    json += ",\"name\":\"";
    json += jsonEscape(ctx.settings.cameraName);
    json += "\",\"known\":";
    json += ctx.camera.valid ? "true" : "false";
    json += ",\"ip\":\"";
    json += ctx.camera.valid ? ctx.camera.ip.toString() : String("");
    json += "\",\"onvifUrl\":\"";
    json += jsonEscape(ctx.camera.onvifUrl);
    json += "\",\"uuid\":\"";
    json += jsonEscape(ctx.camera.endpointUuid);
    json += "\",\"serialNumber\":\"";
    json += jsonEscape(ctx.camera.serialNumber);
    json += "\",\"identityConfirmed\":";
    json += cameraIdentityMatches(ctx.camera, ctx.camera) ? "true" : "false";
    json += ",\"alternateOnvifUrls\":[";
    for (uint8_t index = 0; index < ctx.alternateOnvifCount; index++) {
        if (index != 0)
            json += ',';
        json += '"';
        appendJsonEscaped(json, ctx.alternateOnvifUrls[index]);
        json += '"';
    }
    json += "]";
    json += ",\"scopes\":\"";
    json += jsonEscape(ctx.camera.scopes);
    json += "\",\"rtspPort\":";
    json += ctx.camera.rtspPort;
    json += "},";

    json += "\"onvif\":{";
    json += "\"catalogResolved\":";
    json += ctx.onvifServices.resolved ? "true" : "false";
    json += ",\"usedMediaFallback\":";
    json += ctx.onvifServices.usedMediaFallback ? "true" : "false";
    json += ",\"serviceCount\":";
    json += ctx.onvifServices.serviceCount;
    json += ",\"attempts\":";
    json += ctx.onvifServices.attempts;
    json += ",\"successes\":";
    json += ctx.onvifServices.successes;
    json += ",\"failures\":";
    json += ctx.onvifServices.failures;
    json += ",\"rejectedEndpoints\":";
    json += ctx.onvifServices.rejectedEndpoints;
    json += ",\"rejectedAuthenticationEndpoints\":";
    json += ctx.onvifServices.rejectedAuthenticationEndpoints;
    json += ",\"inaccessibleEndpoints\":";
    json += ctx.onvifServices.inaccessibleEndpoints;
    json += ",\"invalidResponseEndpoints\":";
    json += ctx.onvifServices.invalidResponseEndpoints;
    json += ",\"unvalidatedEndpoints\":";
    json += ctx.onvifServices.unvalidatedEndpoints;
    json += ",\"catalogValid\":";
    json += ctx.onvifServices.catalogValid ? "true" : "false";
    json += ",\"catalogStale\":";
    json += ctx.onvifServices.stale ? "true" : "false";
    json += ",\"lastStatus\":";
    json += ctx.onvifServices.lastStatusCode;
    json += ",\"lastLatencyMs\":";
    json += ctx.onvifServices.lastLatencyMs;
    json += ",\"ageSeconds\":";
    json += ctx.onvifServices.resolvedAtMs == 0 ? 0UL : (now - ctx.onvifServices.resolvedAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.onvifServices.lastResult);
    json += "\",\"authentication\":{";
    json += "\"usernameToken\":";
    json += ctx.onvifServices.usernameToken ? "true" : "false";
    json += ",\"httpDigest\":";
    json += ctx.onvifServices.httpDigest ? "true" : "false";
    json += "},\"capabilities\":{";
    json += "\"snapshotUri\":";
    json += ctx.onvifServices.mediaSnapshotUri ? "true" : "false";
    json += ",\"eventsPullPoint\":";
    json += ctx.onvifServices.eventsPullPoint ? "true" : "false";
    json += ",\"imagingStabilizationAdvertised\":";
    json += ctx.onvifServices.imagingStabilizationAdvertised ? "true" : "false";
    json += ",\"analyticsRules\":";
    json += ctx.onvifServices.analyticsRules ? "true" : "false";
    json += ",\"analyticsModules\":";
    json += ctx.onvifServices.analyticsModules ? "true" : "false";
    json += "},\"services\":{";
    appendOnvifEndpointJson(json, "device", ctx.onvifServices.device, false);
    appendOnvifEndpointJson(json, "media1", ctx.onvifServices.media1, true);
    appendOnvifEndpointJson(json, "media2", ctx.onvifServices.media2, true);
    appendOnvifEndpointJson(json, "events", ctx.onvifServices.events, true);
    appendOnvifEndpointJson(json, "ptz", ctx.onvifServices.ptz, true);
    appendOnvifEndpointJson(json, "imaging", ctx.onvifServices.imaging, true);
    appendOnvifEndpointJson(json, "analytics", ctx.onvifServices.analytics, true);
    appendOnvifEndpointJson(json, "deviceIo", ctx.onvifServices.deviceIo, true);
    json += "},\"ptz\":{";
    json += "\"available\":";
    json += ctx.onvifServices.ptz.available ? "true" : "false";
    json += ",\"validated\":";
    json += ctx.onvifServices.ptz.validated ? "true" : "false";
    json += ",\"profileResolved\":";
    json += ctx.onvifPtz.profileResolved ? "true" : "false";
    json += ",\"profileToken\":\"";
    json += jsonEscape(ctx.onvifPtz.profileToken);
    json += "\",\"profileName\":\"";
    json += jsonEscape(ctx.onvifPtz.profileName);
    json += "\",\"presetsLoaded\":";
    json += ctx.onvifPtz.presetsLoaded ? "true" : "false";
    json += ",\"presetCount\":";
    json += ctx.onvifPtz.presetCount;
    json += ",\"reportedPresetCount\":";
    json += ctx.onvifPtz.reportedPresetCount;
    json += ",\"truncated\":";
    json += ctx.onvifPtz.truncated ? "true" : "false";
    json += ",\"daemonPresetToken\":\"";
    json += jsonEscape(CAMERA_DAEMON_PRESET_TOKEN);
    json += "\",\"refreshAttempts\":";
    json += ctx.onvifPtz.refreshAttempts;
    json += ",\"refreshSuccesses\":";
    json += ctx.onvifPtz.refreshSuccesses;
    json += ",\"refreshFailures\":";
    json += ctx.onvifPtz.refreshFailures;
    json += ",\"lastStatus\":";
    json += ctx.onvifPtz.lastRefreshStatusCode;
    json += ",\"lastLatencyMs\":";
    json += ctx.onvifPtz.lastRefreshLatencyMs;
    json += ",\"ageSeconds\":";
    json += ctx.onvifPtz.refreshedAtMs == 0 ? 0UL : (now - ctx.onvifPtz.refreshedAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.onvifPtz.lastResult);
    json += "\",\"command\":{";
    json += "\"attempts\":";
    json += ctx.onvifPtz.commandAttempts;
    json += ",\"accepted\":";
    json += ctx.onvifPtz.commandAccepted;
    json += ",\"errors\":";
    json += ctx.onvifPtz.commandErrors;
    json += ",\"lastToken\":\"";
    json += jsonEscape(ctx.onvifPtz.lastCommandToken);
    json += "\",\"lastStatus\":";
    json += ctx.onvifPtz.lastCommandStatusCode;
    json += ",\"lastLatencyMs\":";
    json += ctx.onvifPtz.lastCommandLatencyMs;
    json += ",\"ageSeconds\":";
    json += ctx.onvifPtz.lastCommandAtMs == 0 ? 0UL : (now - ctx.onvifPtz.lastCommandAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.onvifPtz.lastCommandResult);
    json += "\"},\"presets\":[";

    for (uint16_t index = 0; index < ctx.onvifPtz.presetCount; index++) {
        if (index > 0) {
            json += ',';
        }

        json += "{\"token\":\"";
        json += jsonEscape(ctx.onvifPtz.presets[index].token);
        json += "\",\"name\":\"";
        json += jsonEscape(ctx.onvifPtz.presets[index].name);
        json += "\",\"daemon\":";
        json += onvifPtzPresetIsDaemon(ctx.onvifPtz.presets[index].token) ? "true" : "false";
        json += '}';
    }

    json += "]},\"deviceInfo\":{";
    json += "\"loaded\":";
    json += ctx.onvifMetadata.loaded ? "true" : "false";
    json += ",\"inventoryComplete\":";
    json += ctx.onvifMetadata.loadedAtMs != 0 ? "true" : "false";
    json += ",\"manufacturer\":\"";
    json += jsonEscape(ctx.onvifMetadata.manufacturer);
    json += "\",\"model\":\"";
    json += jsonEscape(ctx.onvifMetadata.model);
    json += "\",\"firmware\":\"";
    json += jsonEscape(ctx.onvifMetadata.firmware);
    json += "\",\"dateTimeType\":\"";
    json += jsonEscape(ctx.onvifMetadata.dateTimeType);
    json += "\",\"reportedTimeZone\":\"";
    json += jsonEscape(ctx.onvifMetadata.timeZone);
    json += "\",\"scopeCount\":";
    json += ctx.onvifMetadata.scopeCount;
    json += ",\"profileT\":";
    json += ctx.onvifMetadata.profileT ? "true" : "false";
    json += ",\"requests\":";
    json += ctx.onvifMetadata.requests;
    json += ",\"failures\":";
    json += ctx.onvifMetadata.failures;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.onvifMetadata.lastResult);
    json += "\"},\"clock\":{";
    json += "\"checked\":";
    json += ctx.cameraClockStatus.checked ? "true" : "false";
    json += ",\"utcValid\":";
    json += ctx.cameraClockStatus.utcValid ? "true" : "false";
    json += ",\"healthy\":";
    json += (ctx.cameraClockStatus.utcValid && sntpReferenceIsFresh && ctx.cameraClockStatus.driftMs >= -CAMERA_CLOCK_HEALTHY_DRIFT_MS && ctx.cameraClockStatus.driftMs <= CAMERA_CLOCK_HEALTHY_DRIFT_MS) ? "true" : "false";
    json += ",\"referenceSynchronized\":";
    json += sntpReferenceIsFresh ? "true" : "false";
    json += ",\"dateTimeType\":\"";
    json += jsonEscape(ctx.cameraClockStatus.dateTimeType);
    json += "\",\"reportedTimeZone\":\"";
    json += jsonEscape(ctx.cameraClockStatus.timeZone);
    json += "\",\"daylightSavings\":";
    json += ctx.cameraClockStatus.daylightSavings ? "true" : "false";
    json += ",\"driftMilliseconds\":";
    if (ctx.cameraClockStatus.utcValid) {
        json += ctx.cameraClockStatus.driftMs;
    } else {
        json += "null";
    }
    json += ",\"roundTripMs\":";
    if (ctx.cameraClockStatus.utcValid) {
        json += ctx.cameraClockStatus.roundTripMs;
    } else {
        json += "null";
    }
    json += ",\"checks\":";
    json += ctx.cameraClockStatus.checks;
    json += ",\"failures\":";
    json += ctx.cameraClockStatus.failures;
    json += ",\"lastCheckAgeSeconds\":";
    json += ctx.cameraClockStatus.lastCheckAtMs == 0 ? 0UL : (now - ctx.cameraClockStatus.lastCheckAtMs) / 1000UL;
    json += ",\"referenceAgeSeconds\":";
    json += !sntpReference.synchronized || sntpReference.synchronizedAtMs == 0 ? 0UL : (now - sntpReference.synchronizedAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.cameraClockStatus.lastResult);
    json += "\"},\"events\":{";
    json += "\"propertiesLoaded\":";
    json += ctx.onvifMetadata.eventPropertiesLoaded ? "true" : "false";
    json += ",\"topicCount\":";
    json += ctx.onvifMetadata.eventTopicCount;
    json += ",\"descriptionCount\":";
    json += ctx.onvifMetadata.eventDescriptionCount;
    json += ",\"motion\":";
    json += ctx.onvifMetadata.motionEvents ? "true" : "false";
    json += ",\"tamper\":";
    json += ctx.onvifMetadata.tamperEvents ? "true" : "false";
    json += ",\"signalLoss\":";
    json += ctx.onvifMetadata.signalLossEvents ? "true" : "false";
    json += ",\"imageQuality\":";
    json += ctx.onvifMetadata.imageQualityEvents ? "true" : "false";
    json += "}},";

    json += "\"probes\":{";
    json += "\"onvifTcp\":";
    json += ctx.lastOnvifProbe.tcpConnected ? "true" : "false";
    json += ",\"onvifHttp\":";
    json += ctx.lastOnvifProbe.protocolResponded ? "true" : "false";
    json += ",\"onvifStatus\":";
    json += ctx.lastOnvifProbe.statusCode;
    json += ",\"onvifLatencyMs\":";
    json += ctx.lastOnvifProbe.latencyMs;
    json += ",\"rtspTcp\":";
    json += ctx.lastRtspProbe.tcpConnected ? "true" : "false";
    json += ",\"rtspAttempted\":";
    json += ctx.lastRtspProbeAttempted ? "true" : "false";
    json += ",\"rtspProtocol\":";
    json += ctx.lastRtspProbe.protocolResponded ? "true" : "false";
    json += ",\"rtspStatus\":";
    json += ctx.lastRtspProbe.statusCode;
    json += ",\"rtspLatencyMs\":";
    json += ctx.lastRtspProbe.latencyMs;
    json += "},";

    json += "\"healthEvidence\":{\"source\":\"";
    json += healthEvidenceSourceName(ctx.lastHealthEvidenceSource);
    json += "\",\"ageSeconds\":";
    json += ctx.lastHealthEvidenceAtMs == 0 ? 0UL : (now - ctx.lastHealthEvidenceAtMs) / 1000UL;
    json += "},";

    json += "\"live\":";
    appendLiveStreamJson(json);
    json += ",";

    json += "\"snapshot\":{";
    json += "\"ready\":";
    json += ctx.snapshotTarget.valid ? "true" : "false";
    json += ",\"attempted\":";
    json += ctx.lastSnapshotProbe.attempted ? "true" : "false";
    json += ",\"requestAttempted\":";
    json += ctx.lastSnapshotProbe.requestAttempted ? "true" : "false";
    json += ",\"resolutionAttempted\":";
    json += ctx.lastSnapshotProbe.resolutionAttempted ? "true" : "false";
    json += ",\"resolutionTransportFailure\":";
    json += ctx.lastSnapshotProbe.resolutionTransportFailure ? "true" : "false";
    json += ",\"conclusive\":";
    json += ctx.lastSnapshotProbe.conclusive ? "true" : "false";
    json += ",\"profileFailoverPending\":";
    json += ctx.snapshotProfileFailoverPending ? "true" : "false";
    json += ",\"targetResolved\":";
    json += ctx.lastSnapshotProbe.targetResolved ? "true" : "false";
    json += ",\"healthy\":";
    json += ctx.lastSnapshotProbe.jpegValid ? "true" : "false";
    json += ",\"httpResponded\":";
    json += ctx.lastSnapshotProbe.httpResponded ? "true" : "false";
    json += ",\"status\":";
    json += ctx.lastSnapshotProbe.statusCode;
    json += ",\"latencyMs\":";
    json += ctx.lastSnapshotProbe.latencyMs;
    json += ",\"bytesRead\":";
    json += static_cast<unsigned long>(ctx.lastSnapshotProbe.bytesRead);
    json += ",\"profile\":\"";
    json += jsonEscape(ctx.snapshotTarget.profileName);
    json += "\",\"detail\":\"";
    json += jsonEscape(ctx.lastSnapshotProbe.detail);
    json += "\",\"served\":";
    json += ctx.snapshotsServed;
    json += ",\"errors\":";
    json += ctx.snapshotErrors;
    json += ",\"cameraHttpRequests\":";
    json += ctx.cameraJpegHttpRequests;
    json += ",\"cameraHttpConnectionReuses\":";
    json += ctx.cameraJpegConnectionReuses;
    json += ",\"digestPreemptiveRequests\":";
    json += ctx.cameraJpegDigestPreemptiveRequests;
    json += ",\"previewFetchOperations\":";
    json += ctx.previewCameraFetches;
    json += ",\"busyResponses\":";
    json += ctx.previewBusyResponses;
    json += ",\"clientAborts\":";
    json += ctx.previewClientAborts;
    json += ",\"bytesTransferred\":";
    appendUnsigned64(json, ctx.previewBytesTransferred);
    json += ",\"healthEvidenceUses\":";
    json += ctx.previewHealthEvidenceUses;
    json += ",\"lastDeliveredAgeSeconds\":";
    json += ctx.lastBrowserSnapshotDeliveredAtMs == 0 ? 0UL : (now - ctx.lastBrowserSnapshotDeliveredAtMs) / 1000UL;
    json += ",\"lastDeliveredBytes\":";
    json += ctx.lastBrowserSnapshotBytes;
    json += ",\"lastDeliveredLatencyMs\":";
    json += ctx.lastBrowserSnapshotLatencyMs;
    json += ",\"probeRuns\":";
    json += ctx.snapshotProbeRuns;
    json += ",\"probeFailures\":";
    json += ctx.snapshotProbeFailures;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.lastSnapshotResult);
    json += "\"},";

    json += "\"incident\":{";
    json += "\"kind\":\"";
    json += incidentName(ctx.activeIncident);
    json += "\",\"active\":";
    json += ctx.activeIncident != IncidentKind::None ? "true" : "false";
    json += ",\"alertSent\":";
    json += ctx.incidentAlertSent ? "true" : "false";
    json += ",\"pendingAlert\":";
    json += ctx.pendingIncidentEmail ? "true" : "false";
    json += ",\"durationSeconds\":";
    json += ctx.incidentStartedAtMs == 0 ? 0UL : (now - ctx.incidentStartedAtMs) / 1000UL;
    json += "},";

    json += "\"recovery\":{";
    json += "\"autoRebootEnabled\":";
    json += ctx.settings.enableOnvifAutoReboot ? "true" : "false";
    json += ",\"credentialsConfigured\":";
    json += cameraCredentialsConfigured(ctx) ? "true" : "false";
    json += ",\"graceActive\":";
    json += cameraRebootGraceActive(ctx) ? "true" : "false";
    json += ",\"graceRemainingSeconds\":";
    json += cameraRebootGraceRemainingMs(ctx) / 1000UL;
    json += ",\"attempts\":";
    json += ctx.cameraRebootAttempts;
    json += ",\"accepted\":";
    json += ctx.cameraRebootAccepted;
    json += ",\"errors\":";
    json += ctx.cameraRebootErrors;
    json += ",\"automatic\":{";
    json += "\"attempts\":";
    json += ctx.automaticCameraRebootAttempts;
    json += ",\"accepted\":";
    json += ctx.automaticCameraRebootAccepted;
    json += ",\"errors\":";
    json += ctx.automaticCameraRebootErrors;
    json += "},\"manual\":{";
    json += "\"attempts\":";
    json += ctx.manualCameraRebootAttempts;
    json += ",\"accepted\":";
    json += ctx.manualCameraRebootAccepted;
    json += ",\"errors\":";
    json += ctx.manualCameraRebootErrors;
    json += "}";
    json += ",\"attemptsInWindow\":";
    json += ctx.cameraRebootAttemptsInWindow;
    json += ",\"maxAttemptsInWindow\":";
    json += ctx.settings.maxOnvifRebootAttemptsPerWindow;
    json += ",\"lastAttemptAgeSeconds\":";
    json += ctx.lastCameraRebootAttemptAtMs == 0 ? 0UL : (now - ctx.lastCameraRebootAttemptAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(ctx.lastCameraRebootResult);
    json += "\",\"lastEvidence\":\"";
    json += jsonEscape(ctx.lastCameraRebootEvidence);
    json += "\",\"acceptedHistory\":[";

    const size_t cameraRebootHistoryStart = (ctx.cameraRebootHistoryNext + CAMERA_REBOOT_HISTORY_CAPACITY - ctx.cameraRebootHistoryCount) % CAMERA_REBOOT_HISTORY_CAPACITY;

    for (size_t offset = 0; offset < ctx.cameraRebootHistoryCount; offset++) {
        if (offset > 0) {
            json += ',';
        }

        const CameraRebootEvidence& evidence = ctx.cameraRebootHistory[(cameraRebootHistoryStart + offset) % CAMERA_REBOOT_HISTORY_CAPACITY];
        json += "{\"timestamp\":\"";
        json += jsonEscape(evidence.timestamp);
        json += "\",\"uptimeSeconds\":";
        json += evidence.uptimeSeconds;
        json += ",\"origin\":\"";
        json += evidence.manual ? "manual" : "automatic";
        json += "\",\"snapshot\":{";
        json += "\"status\":";
        json += evidence.snapshotStatus;
        json += ",\"latencyMs\":";
        json += evidence.snapshotLatencyMs;
        json += ",\"bytes\":";
        json += evidence.snapshotBytes;
        json += ",\"profile\":\"";
        json += jsonEscape(evidence.snapshotProfile);
        json += "\",\"consecutiveFailures\":";
        json += evidence.consecutiveImageFailures;
        json += "},\"esp32WifiRssi\":";
        json += evidence.esp32WifiRssi;
        json += ",\"freeHeapBytes\":";
        json += evidence.freeHeapBytes;
        json += '}';
    }

    json += "]},";

    json += "\"counters\":{";
    json += "\"totalChecks\":";
    json += ctx.totalHealthChecks;
    json += ",\"successfulChecks\":";
    json += ctx.successfulHealthChecks;
    json += ",\"failedChecks\":";
    json += ctx.failedHealthChecks;
    json += ",\"inconclusiveChecks\":";
    json += ctx.inconclusiveHealthChecks;
    json += ",\"consecutiveFailures\":";
    json += ctx.consecutiveFailures;
    json += ",\"consecutiveControlFailures\":";
    json += ctx.consecutiveControlFailures;
    json += ",\"consecutiveInconclusiveChecks\":";
    json += ctx.consecutiveInconclusiveChecks;
    json += ",\"consecutiveMediaServiceFailures\":";
    json += ctx.consecutiveMediaServiceFailures;
    json += ",\"consecutiveSuccesses\":";
    json += ctx.consecutiveSuccesses;
    json += ",\"discoveryRuns\":";
    json += discoveryRuns;
    json += ",\"subnetScanRuns\":";
    json += subnetScanRuns;
    json += ",\"emailsSent\":";
    json += ctx.emailsSent;
    json += ",\"emailErrors\":";
    json += ctx.emailErrors;
    json += "},";

    json += "\"lastDiscovery\":\"";
    json += jsonEscape(lastDiscoveryResult);
    json += "\",\"lastSubnetScan\":\"";
    json += jsonEscape(lastScanResult);
    json += "\",\"lastEmailResult\":\"";
    json += jsonEscape(ctx.lastEmailResult);
    json += "\",\"resendConfigured\":";
    json += resendConfigured() ? "true" : "false";
    json += ",\"ui\":{";
    json += "\"feedMinimumIntervalMs\":";
    json += ctx.settings.feedMinIntervalMs;
    json += ",\"feedTargetCycleMs\":";
    json += ctx.settings.feedTargetCycleMs;
    json += ",\"previewMaximumFps\":";
    json += String(1000.0f / ctx.settings.feedTargetCycleMs, 2);
    json += ",\"statusRefreshIntervalMs\":";
    json += globalSettings.statusRefreshIntervalMs;
    json += ",\"snapshotHttpTimeoutMs\":";
    json += ctx.settings.snapshotHttpTimeoutMs;
    json += "},\"settings\":{";
    json += "\"defaultsRevision\":";
    json += RUNTIME_SETTINGS_DEFAULTS_REVISION;
    json += ",\"revision\":";
    json += settingsRevision;
    json += ",\"source\":\"";
    json += settingsLoadedFromNvs ? "nvs" : "factory";
    json += "\",\"lastResult\":\"";
    json += jsonEscape(lastSettingsResult);
    json += "\"},\"factoryResetState\":\"";
    json += factoryResetStateName();
    json += "\",\"adminAction\":{";
    json += "\"pending\":\"";
    json += adminActionName(currentAdminAction());
    json += "\",\"lastResult\":\"";
    json += jsonEscape(lastAdminActionResult);
    json += "\"}}";

    return json;
}

bool persistentLogTimeSynchronized(const PersistentLog::Entry& entry)
{
    return entry.epochSeconds >= 1700000000UL;
}

String persistentLogUtcTimestamp(const PersistentLog::Entry& entry)
{
    if (!persistentLogTimeSynchronized(entry)) {
        return String();
    }

    const time_t timestamp = static_cast<time_t>(entry.epochSeconds);
    struct tm utcTime;
    gmtime_r(&timestamp, &utcTime);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return String(buffer);
}

String persistentLogTimestamp(const PersistentLog::Entry& entry)
{
    const String utcTimestamp = persistentLogUtcTimestamp(entry);
    return utcTimestamp.isEmpty() ? String("uptime-") + String(entry.uptimeMs / 1000UL) + "s" : utcTimestamp;
}

uint32_t persistentLogTimeoutMs(const PersistentLog::Entry& entry)
{
    switch (entry.event) {
        case PersistentLog::Event::OnvifProbe:
        case PersistentLog::Event::RtspProbe:
            return entry.value;
        default:
            return 0;
    }
}

String persistentLogTargetIp(const PersistentLog::Entry& entry)
{
    return entry.targetIp == 0 ? String() : uint32ToIp(entry.targetIp).toString();
}

void appendPersistentLogJson(String& json, const PersistentLog::Entry& entry)
{
    json += "{\"id\":";
    json += entry.sequence;
    json += ",\"bootId\":";
    json += entry.bootId;
    json += ",\"contextId\":";
    json += entry.contextId;
    json += ",\"cameraId\":";
    json += entry.cameraId;
    json += ",\"cameraName\":\"";
    const CameraContext* sourceCamera = findCamera(entry.cameraId);
    appendJsonEscaped(json, sourceCamera != nullptr ? String(sourceCamera->settings.cameraName) : entry.cameraId == 0 ? String("System") : String("Camera ") + entry.cameraId);
    json += '"';
    json += ",\"timestamp\":\"";
    json += persistentLogTimestamp(entry);
    json += "\",\"timestampUtc\":\"";
    json += persistentLogUtcTimestamp(entry);
    json += "\",\"timeSynchronized\":";
    json += persistentLogTimeSynchronized(entry) ? "true" : "false";
    json += ",\"uptimeMs\":";
    json += entry.uptimeMs;
    json += ",\"level\":\"";
    json += PersistentLog::levelName(entry.level);
    json += "\",\"category\":\"";
    json += PersistentLog::categoryName(entry.category);
    json += "\",\"event\":\"";
    json += PersistentLog::eventName(entry.event);
    json += "\",\"outcome\":\"";
    json += PersistentLog::outcomeName(entry.outcome);
    json += "\",\"targetIp\":\"";
    json += persistentLogTargetIp(entry);
    json += "\",\"targetPort\":";
    json += entry.targetPort;
    json += ",\"status\":";
    json += entry.statusCode;
    json += ",\"durationMs\":";
    json += entry.durationMs;
    json += ",\"timeoutMs\":";
    json += persistentLogTimeoutMs(entry);
    json += ",\"value\":";
    json += entry.value;
    json += ",\"detail\":\"";
    json += jsonEscape(String(entry.detail));
    json += "\",\"flags\":{\"outbound\":";
    json += (entry.flags & PersistentLog::FlagOutbound) ? "true" : "false";
    json += ",\"connectionReused\":";
    json += (entry.flags & PersistentLog::FlagConnectionReused) ? "true" : "false";
    json += ",\"authenticationRetry\":";
    json += (entry.flags & PersistentLog::FlagAuthenticationRetry) ? "true" : "false";
    json += ",\"manual\":";
    json += (entry.flags & PersistentLog::FlagManual) ? "true" : "false";
    json += ",\"scheduled\":";
    json += (entry.flags & PersistentLog::FlagScheduled) ? "true" : "false";
    json += "}}";
}

void handlePersistentLogs()
{
    PersistentLog::flush();

    size_t offset = 0;
    size_t limit = 100;

    if (httpServer.hasArg("offset")) {
        offset = static_cast<size_t>(max(httpServer.arg("offset").toInt(), 0L));
    }

    if (httpServer.hasArg("limit")) {
        limit = static_cast<size_t>(constrain(httpServer.arg("limit").toInt(), 1L, 100L));
    }

    const size_t total = PersistentLog::count();
    String json;
    json.reserve(1024 + limit * 384);
    json += "{\"ready\":";
    json += PersistentLog::ready() ? "true" : "false";
    json += ",\"total\":";
    json += total;
    json += ",\"capacity\":";
    json += PersistentLog::capacity();
    json += ",\"offset\":";
    json += offset;
    json += ",\"storage\":{\"totalBytes\":";
    json += PersistentLog::storageTotalBytes();
    json += ",\"usedBytes\":";
    json += PersistentLog::storageUsedBytes();
    json += ",\"fileBytes\":";
    json += PersistentLog::storageFileBytes();
    json += ",\"pending\":";
    json += PersistentLog::pendingCount();
    json += ",\"dropped\":";
    json += PersistentLog::droppedCount();
    json += ",\"writeErrors\":";
    json += PersistentLog::writeErrorCount();
    json += "},\"entries\":[";

    size_t returned = 0;

    size_t index = offset;
    for (; index < total && returned < limit; index++) {
        PersistentLog::Entry entry;

        if (!PersistentLog::readNewest(index, entry)) {
            continue;
        }

        if (returned++ > 0) {
            json += ',';
        }

        appendPersistentLogJson(json, entry);
    }

    json += "],\"returned\":";
    json += returned;
    json += ",\"nextOffset\":";
    json += index;
    json += ",\"hasMore\":";
    json += index < total ? "true" : "false";
    json += '}';
    sendJsonResponse(200, json);
}

void appendCsvText(String& output, const String& value)
{
    output += '"';

    if (!value.isEmpty() && (value.charAt(0) == '=' || value.charAt(0) == '+' || value.charAt(0) == '-' || value.charAt(0) == '@')) {
        output += '\'';
    }

    for (size_t index = 0; index < value.length(); index++) {
        const char character = value.charAt(index);

        if (character == '"') {
            output += "\"\"";
        } else if (character == '\r' || character == '\n') {
            output += ' ';
        } else {
            output += character;
        }
    }

    output += '"';
}

void appendPersistentLogCsv(String& csv, const PersistentLog::Entry& entry)
{
    csv += entry.sequence;
    csv += ';';
    appendCsvText(csv, persistentLogTimestamp(entry));
    csv += ';';
    csv += entry.uptimeMs;
    csv += ';';
    appendCsvText(csv, PersistentLog::levelName(entry.level));
    csv += ';';
    appendCsvText(csv, PersistentLog::categoryName(entry.category));
    csv += ';';
    appendCsvText(csv, PersistentLog::eventName(entry.event));
    csv += ';';
    appendCsvText(csv, PersistentLog::outcomeName(entry.outcome));
    csv += ';';
    appendCsvText(csv, persistentLogTargetIp(entry));
    csv += ';';
    csv += entry.targetPort;
    csv += ';';
    csv += entry.statusCode;
    csv += ';';
    csv += entry.durationMs;
    csv += ';';
    csv += entry.value;
    csv += ';';
    csv += (entry.flags & PersistentLog::FlagOutbound) ? '1' : '0';
    csv += ';';
    csv += (entry.flags & PersistentLog::FlagConnectionReused) ? '1' : '0';
    csv += ';';
    csv += (entry.flags & PersistentLog::FlagAuthenticationRetry) ? '1' : '0';
    csv += ';';
    csv += (entry.flags & PersistentLog::FlagManual) ? '1' : '0';
    csv += ';';
    csv += (entry.flags & PersistentLog::FlagScheduled) ? '1' : '0';
    csv += ';';
    appendCsvText(csv, String(entry.detail));
    csv += ';';
    csv += entry.bootId;
    csv += ';';
    csv += entry.contextId;
    csv += ';';
    appendCsvText(csv, persistentLogUtcTimestamp(entry));
    csv += ';';
    csv += persistentLogTimeSynchronized(entry) ? '1' : '0';
    csv += ';';
    csv += persistentLogTimeoutMs(entry);
    csv += ';';
    csv += entry.cameraId;
    csv += ';';
    const CameraContext* sourceCamera = findCamera(entry.cameraId);
    appendCsvText(csv, sourceCamera != nullptr ? String(sourceCamera->settings.cameraName) : entry.cameraId == 0 ? String("System") : String("Camera ") + entry.cameraId);
    csv += "\r\n";
}

void handlePersistentLogsExport()
{
    PersistentLog::flush();
    const size_t total = PersistentLog::count();
    httpServer.sendHeader("Content-Disposition", "attachment; filename=\"camera-watchdog-logs.csv\"");
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer.send(200, "text/csv; charset=utf-8", "");

    String chunk;
    chunk.reserve(2048);
    chunk += "id;timestamp;uptime_ms;level;category;event;outcome;";
    chunk += "target_ip;target_port;status;duration_ms;value;outbound;";
    chunk += "connection_reused;authentication_retry;manual;scheduled;detail;";
    chunk += "boot_id;context_id;timestamp_utc;time_synchronized;timeout_ms;camera_id;camera_name\r\n";

    for (size_t chronological = 0; chronological < total; chronological++) {
        const size_t newestOffset = total - chronological - 1;
        PersistentLog::Entry entry;

        if (!PersistentLog::readNewest(newestOffset, entry)) {
            continue;
        }

        appendPersistentLogCsv(chunk, entry);

        if (chunk.length() >= 1536) {
            httpServer.sendContent(chunk);
            chunk.remove(0);
            updateCameraAlertLed();
            delay(0);
        }
    }

    if (!chunk.isEmpty()) {
        httpServer.sendContent(chunk);
    }

    httpServer.sendContent("");
}

void sendJsonResponse(int statusCode, const String& json)
{
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(statusCode, "application/json; charset=utf-8", json);
}

void sendApiError(int statusCode, const char* code, const String& detail)
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

bool authorizeDashboardMutation(String& error, bool blockDuringHealthCheck)
{
    if (blockDuringHealthCheck && (healthCheckInProgress || registryBusy || cameraIoDepth != 0)) {
        error = "Health check in progress; try again";
        return false;
    }

    if (!httpServer.hasArg("writeToken")) {
        error = "Write token missing; reload the Settings tab";
        return false;
    }

    if (httpServer.arg("writeToken") != settingsWriteToken) {
        error = "Invalid write token; reload the page";
        return false;
    }

    if (adminPasswordConfigured() && (!httpServer.hasArg("adminPassword") || !secureTextEquals(httpServer.arg("adminPassword"), DASHBOARD_ADMIN_PASSWORD_VALUE))) {
        error = "Incorrect admin password";
        return false;
    }

    return true;
}

bool authorizeSettingsMutation(String& error)
{
    if (!authorizeDashboardMutation(error, true)) {
        return false;
    }

    if (factoryResetRequestedAtMs != 0) {
        error = "Factory data reset in progress";
        return false;
    }

    if (currentAdminAction() != AdminAction::None) {
        error = "Wait for the admin action to finish before changing the settings";
        return false;
    }

    return true;
}

bool readStringSettingArgument(const char* name, size_t minimumLength, size_t maximumLength, String& value, String& error, bool trim = true)
{
    if (!httpServer.hasArg(name)) {
        error = String("Missing field: ") + name;
        return false;
    }

    value = httpServer.arg(name);
    if (trim) {
        value.trim();
    }

    if (value.length() < minimumLength || value.length() > maximumLength) {
        error = String("Field outside the allowed length: ") + name;
        return false;
    }

    for (size_t index = 0; index < value.length(); index++) {
        const uint8_t byte = static_cast<uint8_t>(value.charAt(index));

        if (byte < 0x20 || byte == 0x7F) {
            error = String("Field contains an invalid character: ") + name;
            return false;
        }
    }

    return true;
}

bool readUnsignedSettingArgument(const char* name, uint32_t minimum, uint32_t maximum, uint32_t& value, String& error)
{
    if (!httpServer.hasArg(name)) {
        error = String("Missing field: ") + name;
        return false;
    }

    String textValue = httpServer.arg(name);
    textValue.trim();

    if (textValue.isEmpty()) {
        error = String("Empty field: ") + name;
        return false;
    }

    uint64_t parsed = 0;

    for (size_t index = 0; index < textValue.length(); index++) {
        const char character = textValue.charAt(index);

        if (character < '0' || character > '9') {
            error = String("Invalid numeric field: ") + name;
            return false;
        }

        parsed = parsed * 10ULL + static_cast<uint8_t>(character - '0');

        if (parsed > maximum) {
            error = String("Field above the limit: ") + name;
            return false;
        }
    }

    if (parsed < minimum) {
        error = String("Field below the limit: ") + name;
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

bool readBooleanSettingArgument(const char* name, uint8_t& value, String& error)
{
    if (!httpServer.hasArg(name)) {
        error = String("Missing field: ") + name;
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

    error = String("Invalid boolean option: ") + name;
    return false;
}

bool parseRuntimeSettingsRequest(RuntimeSettings& candidate, String& error)
{
    String textValue;
    uint32_t numericValue = 0;

    if (!readStringSettingArgument("cameraName", CAMERA_NAME_MIN_LENGTH, CAMERA_NAME_MAX_LENGTH, textValue, error)) {
        return false;
    }
    copySettingText(candidate.cameraName, textValue);

    if (!readStringSettingArgument("snapshotProfileMatch", 0, SNAPSHOT_PROFILE_MATCH_MAX_LENGTH, textValue, error)) {
        return false;
    }
    copySettingText(candidate.snapshotProfileMatch, textValue);

    if (!readStringSettingArgument("cameraUsername", 0, CAMERA_USERNAME_MAX_LENGTH, textValue, error, false)) {
        return false;
    }
    copySettingText(candidate.cameraUsername, textValue);

    String passwordAction;
    if (!readStringSettingArgument("cameraPasswordAction", 4, 7, passwordAction, error, false)) {
        return false;
    }

    if (!readStringSettingArgument("cameraPassword", 0, CAMERA_PASSWORD_MAX_LENGTH, textValue, error, false)) {
        return false;
    }

    if (passwordAction == "replace") {
        copySettingText(candidate.cameraPassword, textValue);
    } else if (passwordAction == "keep" || passwordAction == "clear") {
        if (!textValue.isEmpty()) {
            error = "Select replace password to send a new password";
            return false;
        }

        if (passwordAction == "clear") {
            if (candidate.cameraUsername[0] != '\0') {
                error = "Removing the credentials requires an empty username";
                return false;
            }
            copySettingText(candidate.cameraPassword, "");
        }
    } else {
        error = "Invalid camera password action";
        return false;
    }

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

    READ_U16(cameraRtspPort, "cameraRtspPort", CAMERA_RTSP_PORT_MIN, CAMERA_RTSP_PORT_MAX);
    READ_U32(healthCheckIntervalMs, "healthCheckIntervalMs", HEALTH_CHECK_INTERVAL_MIN_MS, HEALTH_CHECK_INTERVAL_MAX_MS);
    READ_U32(startupGraceMs, "startupGraceMs", STARTUP_GRACE_MIN_MS, STARTUP_GRACE_MAX_MS);
    READ_U8(failuresBeforeRediscovery, "failuresBeforeRediscovery", FAILURES_BEFORE_REDISCOVERY_MIN, FAILURES_BEFORE_REDISCOVERY_MAX);
    READ_U8(failuresBeforeOffline, "failuresBeforeOffline", FAILURES_BEFORE_OFFLINE_MIN, FAILURES_BEFORE_OFFLINE_MAX);
    READ_U8(successesBeforeRecovery, "successesBeforeRecovery", SUCCESSES_BEFORE_RECOVERY_MIN, SUCCESSES_BEFORE_RECOVERY_MAX);
    READ_U8(videoFailuresBeforeAlert, "videoFailuresBeforeAlert", VIDEO_FAILURES_BEFORE_ALERT_MIN, VIDEO_FAILURES_BEFORE_ALERT_MAX);
    READ_U8(videoFailuresBeforeReboot, "videoFailuresBeforeReboot", VIDEO_FAILURES_BEFORE_REBOOT_MIN, VIDEO_FAILURES_BEFORE_REBOOT_MAX);

    READ_BOOL(enableOnvifAutoReboot, "enableOnvifAutoReboot");
    READ_U32(onvifRebootGraceMs, "onvifRebootGraceMs", ONVIF_REBOOT_GRACE_MIN_MS, ONVIF_REBOOT_GRACE_MAX_MS);
    READ_U32(onvifRebootCooldownMs, "onvifRebootCooldownMs", ONVIF_REBOOT_COOLDOWN_MIN_MS, ONVIF_REBOOT_COOLDOWN_MAX_MS);
    READ_U32(onvifRebootWindowMs, "onvifRebootWindowMs", ONVIF_REBOOT_WINDOW_MIN_MS, ONVIF_REBOOT_WINDOW_MAX_MS);
    READ_U8(maxOnvifRebootAttemptsPerWindow, "maxOnvifRebootAttemptsPerWindow", MAX_ONVIF_REBOOT_ATTEMPTS_MIN, MAX_ONVIF_REBOOT_ATTEMPTS_MAX);

    READ_BOOL(enableEmailAlerts, "enableEmailAlerts");
    READ_BOOL(emailOnIpChange, "emailOnIpChange");

    READ_U32(tcpConnectTimeoutMs, "tcpConnectTimeoutMs", TCP_CONNECT_TIMEOUT_MIN_MS, TCP_CONNECT_TIMEOUT_MAX_MS);
    READ_U32(serviceResponseTimeoutMs, "serviceResponseTimeoutMs", SERVICE_RESPONSE_TIMEOUT_MIN_MS, SERVICE_RESPONSE_TIMEOUT_MAX_MS);
    READ_U32(snapshotHttpTimeoutMs, "snapshotHttpTimeoutMs", SNAPSHOT_HTTP_TIMEOUT_MIN_MS, SNAPSHOT_HTTP_TIMEOUT_MAX_MS);
    READ_U32(snapshotResolveRetryIntervalMs, "snapshotResolveRetryIntervalMs", SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS);
    READ_U8(snapshotFailuresBeforeReresolve, "snapshotFailuresBeforeReresolve", SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX);

    READ_U32(feedMinIntervalMs, "feedMinIntervalMs", FEED_MIN_INTERVAL_MIN_MS, FEED_MIN_INTERVAL_MAX_MS);
    READ_U32(feedTargetCycleMs, "feedTargetCycleMs", FEED_TARGET_CYCLE_MIN_MS, FEED_TARGET_CYCLE_MAX_MS);
#undef READ_BOOL
#undef READ_U8
#undef READ_U16
#undef READ_U32

    return validateRuntimeSettings(candidate, error);
}

bool parseGlobalSettingsRequest(GlobalSettings& candidate, String& error)
{
    String textValue;
    uint32_t numericValue = 0;
    if (!readStringSettingArgument("localHostname", LOCAL_HOSTNAME_MIN_LENGTH, LOCAL_HOSTNAME_MAX_LENGTH, textValue, error)) {
        return false;
    }
    copySettingText(candidate.localHostname, textValue);
#define READ_U32(field, argument, minimum, maximum) \
    if (!readUnsignedSettingArgument( \
        argument, minimum, maximum, numericValue, error \
    )) { \
        return false; \
    } \
    candidate.field = numericValue

#define READ_BOOL(field, argument) \
    if (!readBooleanSettingArgument(argument, candidate.field, error)) { \
        return false; \
    }

    READ_U32(wifiConnectTimeoutMs, "wifiConnectTimeoutMs", WIFI_CONNECT_TIMEOUT_MIN_MS, WIFI_CONNECT_TIMEOUT_MAX_MS);
    READ_BOOL(enableAutomaticSubnetScan, "enableAutomaticSubnetScan");
    READ_U32(discoveryTimeoutMs, "discoveryTimeoutMs", DISCOVERY_TIMEOUT_MIN_MS, DISCOVERY_TIMEOUT_MAX_MS);
    READ_U32(rediscoveryIntervalMs, "rediscoveryIntervalMs", REDISCOVERY_INTERVAL_MIN_MS, REDISCOVERY_INTERVAL_MAX_MS);
    READ_U32(subnetScanIntervalMs, "subnetScanIntervalMs", SUBNET_SCAN_INTERVAL_MIN_MS, SUBNET_SCAN_INTERVAL_MAX_MS);
    READ_U32(emailRetryIntervalMs, "emailRetryIntervalMs", EMAIL_RETRY_INTERVAL_MIN_MS, EMAIL_RETRY_INTERVAL_MAX_MS);
    READ_U32(scanConnectTimeoutMs, "scanConnectTimeoutMs", SCAN_CONNECT_TIMEOUT_MIN_MS, SCAN_CONNECT_TIMEOUT_MAX_MS);
    READ_BOOL(enableCameraAlertLed, "enableCameraAlertLed");
    READ_U32(cameraAlertLedBlinkIntervalMs, "cameraAlertLedBlinkIntervalMs", CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS, CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS);
    READ_U32(statusRefreshIntervalMs, "statusRefreshIntervalMs", STATUS_REFRESH_INTERVAL_MIN_MS, STATUS_REFRESH_INTERVAL_MAX_MS);
#undef READ_BOOL
#undef READ_U32
    return validateGlobalSettings(candidate, error);
}

String buildSettingsJson(CameraContext* ctx)
{
    String json;
    json.reserve(2400);
    json += "{\"ok\":true,\"schemaVersion\":";
    json += RUNTIME_SETTINGS_SCHEMA_VERSION;
    json += ",\"defaultsRevision\":";
    json += RUNTIME_SETTINGS_DEFAULTS_REVISION;
    json += ",\"scope\":\"";
    json += ctx == nullptr ? "global" : "camera";
    json += "\",\"cameraId\":";
    json += ctx == nullptr ? 0 : ctx->id;
    json += ",\"source\":\"";
    json += (ctx == nullptr ? settingsLoadedFromNvs : ctx->settingsLoadedFromNvs) ? "nvs" : "factory";
    json += "\",\"revision\":";
    json += settingsRevision;
    json += ",\"writeCount\":";
    json += settingsWriteCount;
    json += ",\"lastResult\":\"";
    appendJsonEscaped(json, lastSettingsResult);
    json += "\",\"security\":{\"passwordRequired\":";
    json += adminPasswordConfigured() ? "true" : "false";
    json += ",\"writeToken\":\"";
    json += settingsWriteToken;
    json += "\",\"cameraPasswordConfigured\":";
    json += ctx != nullptr && ctx->settings.cameraPassword[0] != '\0' ? "true" : "false";
    json += "},\"values\":{";
    if (ctx == nullptr) {
        json += "\"wifiConnectTimeoutMs\":";
        json += globalSettings.wifiConnectTimeoutMs;
        json += ",\"enableAutomaticSubnetScan\":";
        json += globalSettings.enableAutomaticSubnetScan;
        json += ",\"discoveryTimeoutMs\":";
        json += globalSettings.discoveryTimeoutMs;
        json += ",\"rediscoveryIntervalMs\":";
        json += globalSettings.rediscoveryIntervalMs;
        json += ",\"subnetScanIntervalMs\":";
        json += globalSettings.subnetScanIntervalMs;
        json += ",\"emailRetryIntervalMs\":";
        json += globalSettings.emailRetryIntervalMs;
        json += ",\"scanConnectTimeoutMs\":";
        json += globalSettings.scanConnectTimeoutMs;
        json += ",\"enableCameraAlertLed\":";
        json += globalSettings.enableCameraAlertLed;
        json += ",\"cameraAlertLedBlinkIntervalMs\":";
        json += globalSettings.cameraAlertLedBlinkIntervalMs;
        json += ",\"statusRefreshIntervalMs\":";
        json += globalSettings.statusRefreshIntervalMs;
        json += ",\"localHostname\":";
        json += '"';
        appendJsonEscaped(json, globalSettings.localHostname);
        json += '"';
    } else {
        json += "\"cameraName\":";
        json += '"';
        appendJsonEscaped(json, ctx->settings.cameraName);
        json += '"';
        json += ",\"snapshotProfileMatch\":";
        json += '"';
        appendJsonEscaped(json, ctx->settings.snapshotProfileMatch);
        json += '"';
        json += ",\"cameraRtspPort\":";
        json += ctx->settings.cameraRtspPort;
        json += ",\"healthCheckIntervalMs\":";
        json += ctx->settings.healthCheckIntervalMs;
        json += ",\"startupGraceMs\":";
        json += ctx->settings.startupGraceMs;
        json += ",\"failuresBeforeRediscovery\":";
        json += ctx->settings.failuresBeforeRediscovery;
        json += ",\"failuresBeforeOffline\":";
        json += ctx->settings.failuresBeforeOffline;
        json += ",\"successesBeforeRecovery\":";
        json += ctx->settings.successesBeforeRecovery;
        json += ",\"videoFailuresBeforeAlert\":";
        json += ctx->settings.videoFailuresBeforeAlert;
        json += ",\"videoFailuresBeforeReboot\":";
        json += ctx->settings.videoFailuresBeforeReboot;
        json += ",\"enableOnvifAutoReboot\":";
        json += ctx->settings.enableOnvifAutoReboot;
        json += ",\"onvifRebootGraceMs\":";
        json += ctx->settings.onvifRebootGraceMs;
        json += ",\"onvifRebootCooldownMs\":";
        json += ctx->settings.onvifRebootCooldownMs;
        json += ",\"onvifRebootWindowMs\":";
        json += ctx->settings.onvifRebootWindowMs;
        json += ",\"maxOnvifRebootAttemptsPerWindow\":";
        json += ctx->settings.maxOnvifRebootAttemptsPerWindow;
        json += ",\"enableEmailAlerts\":";
        json += ctx->settings.enableEmailAlerts;
        json += ",\"emailOnIpChange\":";
        json += ctx->settings.emailOnIpChange;
        json += ",\"tcpConnectTimeoutMs\":";
        json += ctx->settings.tcpConnectTimeoutMs;
        json += ",\"serviceResponseTimeoutMs\":";
        json += ctx->settings.serviceResponseTimeoutMs;
        json += ",\"snapshotHttpTimeoutMs\":";
        json += ctx->settings.snapshotHttpTimeoutMs;
        json += ",\"snapshotResolveRetryIntervalMs\":";
        json += ctx->settings.snapshotResolveRetryIntervalMs;
        json += ",\"snapshotFailuresBeforeReresolve\":";
        json += ctx->settings.snapshotFailuresBeforeReresolve;
        json += ",\"feedMinIntervalMs\":";
        json += ctx->settings.feedMinIntervalMs;
        json += ",\"feedTargetCycleMs\":";
        json += ctx->settings.feedTargetCycleMs;
        json += ",\"cameraUsername\":";
        json += '"';
        appendJsonEscaped(json, ctx->settings.cameraUsername);
        json += '"';
    }
    json += "}}";
    return json;
}

CameraContext* requestedCamera()
{
    if (!httpServer.hasArg("cameraId")) {
        sendApiError(400, "camera_required", "Camera identifier missing");
        return nullptr;
    }
    uint32_t id = 0;
    String error;
    if (!readUnsignedSettingArgument("cameraId", 1, UINT32_MAX, id, error)) {
        sendApiError(422, "invalid_camera", error);
        return nullptr;
    }
    CameraContext* ctx = findCamera(id);
    if (ctx == nullptr) {
        uint32_t primaryId = 0;
        if (cameraIdWasMerged(id, &primaryId))
            sendApiError(410, "camera_merged", String("This ID was merged into registration ") + primaryId + "; update the selection");
        else
            sendApiError(404, "camera_not_found", "Camera not registered");
    }
    return ctx;
}

bool readSettingsScope(bool& globalScope)
{
    const String scope = httpServer.arg("scope");
    if (scope != "global" && scope != "camera") {
        sendApiError(422, "invalid_scope", "Provide the global or camera scope");
        return false;
    }
    globalScope = scope == "global";
    return true;
}

void handleCameraRegistry()
{
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer.send(200, "application/json; charset=utf-8", "");
    String json;
    json.reserve(1024);
    json += "{\"ok\":true,\"firmwareVersion\":\"";
    json += FIRMWARE_VERSION;
    json += "\",\"revision\":";
    json += registryRevision;
    json += ",\"total\":";
    json += cameraCount;
    json += ",\"uptimeSeconds\":";
    json += millis() / 1000UL;
    json += ",\"statusRefreshIntervalMs\":";
    json += globalSettings.statusRefreshIntervalMs;
    json += ",\"wifiConnected\":";
    json += WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"heapFreeBytes\":";
    json += ESP.getFreeHeap();
    json += ",\"heapMinimumFreeBytes\":";
    json += ESP.getMinFreeHeap();
    json += ",\"heapLargestBlockBytes\":";
    json += ESP.getMaxAllocHeap();
    json += ",\"lastResult\":\"";
    appendJsonEscaped(json, lastRegistryResult);
    json += "\",\"factoryResetState\":\"";
    json += factoryResetStateName();
    json += "\",\"adminAction\":{\"pending\":\"";
    json += adminActionName(currentAdminAction());
    json += "\",\"cameraId\":";
    json += activeAdminCameraId != 0 ? activeAdminCameraId : pendingAdminCameraId;
    json += ",\"completed\":";
    json += batchCompleted;
    json += ",\"accepted\":";
    json += batchAccepted;
    json += ",\"failed\":";
    json += batchFailed;
    json += ",\"lastResult\":\"";
    appendJsonEscaped(json, lastAdminActionResult);
    json += "\"},\"cameras\":[";
    httpServer.sendContent(json);
    bool first = true;
    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        json = first ? "{" : ",{";
        first = false;
        json += "\"id\":";
        json += ctx->id;
        json += ",\"name\":\"";
        appendJsonEscaped(json, ctx->settings.cameraName);
        json += "\",\"ip\":\"";
        json += ctx->camera.ip.toString();
        json += "\",\"state\":\"";
        json += stateName(ctx->cameraState);
        json += "\",\"lastCheckAgeSeconds\":";
        if (ctx->lastHealthCheckAtMs == 0)
            json += "null";
        else
            json += (millis() - ctx->lastHealthCheckAtMs) / 1000UL;
        json += ",\"healthCheckIntervalMs\":";
        json += ctx->settings.healthCheckIntervalMs;
        json += ",\"feedTargetCycleMs\":";
        json += ctx->settings.feedTargetCycleMs;
        json += ",\"credentialsConfigured\":";
        json += ctx->settings.cameraUsername[0] != '\0' || ctx->settings.cameraPassword[0] != '\0' ? "true" : "false";
        json += ",\"preferredUrl\":\"";
        appendJsonEscaped(json, ctx->camera.onvifUrl);
        json += "\",\"alternateUrls\":[";
        for (uint8_t index = 0; index < ctx->alternateOnvifCount; index++) {
            if (index != 0)
                json += ',';
            json += '"';
            appendJsonEscaped(json, ctx->alternateOnvifUrls[index]);
            json += '"';
        }
        json += "]";
        json += ",\"identityConfirmed\":";
        json += cameraIdentityMatches(ctx->camera, ctx->camera) ? "true" : "false";
        json += ",\"successfulChecks\":";
        json += ctx->successfulHealthChecks;
        json += ",\"failedChecks\":";
        json += ctx->failedHealthChecks;
        json += ",\"inconclusiveChecks\":";
        json += ctx->inconclusiveHealthChecks;
        json += ",\"lastEvent\":\"";
        appendJsonEscaped(json, ctx->lastEvent);
        json += "\"}";
        httpServer.sendContent(json);
    }
    httpServer.sendContent("]}");
    httpServer.sendContent("");
}

void handleSaveSettings()
{
    String error;
    if (!authorizeSettingsMutation(error)) {
        sendApiError(403, "forbidden", error);
        return;
    }
    bool globalScope = false;
    if (!readSettingsScope(globalScope))
        return;
    CameraContext* ctx = globalScope ? nullptr : requestedCamera();
    if (!globalScope && ctx == nullptr)
        return;
    if (globalScope) {
        GlobalSettings candidate = globalSettings;
        if (!parseGlobalSettingsRequest(candidate, error)) {
            sendApiError(422, "invalid_settings", error);
            return;
        }
        if (!saveGlobalSettingsToNvs(candidate)) {
            sendApiError(500, "nvs_write_failed", "Failed to write global settings");
            return;
        }
        const GlobalSettings previous = globalSettings;
        globalSettings = candidate;
        settingsLoadedFromNvs = true;
        applyGlobalSettings(previous);
    } else {
        RuntimeSettings candidate = ctx->settings;
        if (!parseRuntimeSettingsRequest(candidate, error)) {
            sendApiError(422, "invalid_settings", error);
            return;
        }
        if (!saveRuntimeSettingsToNvs(*ctx, candidate)) {
            sendApiError(500, "nvs_write_failed", "Failed to write camera settings");
            return;
        }
        const RuntimeSettings previous = ctx->settings;
        ctx->settings = candidate;
        ctx->settingsLoadedFromNvs = true;
        applyRuntimeSettings(*ctx, previous);
    }
    settingsRevision++;
    settingsWriteCount++;
    lastSettingsResult = currentTimestamp() + " - settings saved";
    sendJsonResponse(200, buildSettingsJson(ctx));
}

void handleResetSettings()
{
    String error;
    if (!authorizeSettingsMutation(error)) {
        sendApiError(403, "forbidden", error);
        return;
    }
    bool globalScope = false;
    if (!readSettingsScope(globalScope))
        return;
    CameraContext* ctx = globalScope ? nullptr : requestedCamera();
    if (!globalScope && ctx == nullptr)
        return;
    if (globalScope) {
        const GlobalSettings defaults = makeDefaultGlobalSettings();
        if (!saveGlobalSettingsToNvs(defaults)) {
            sendApiError(500, "nvs_write_failed", "Failed to restore global defaults");
            return;
        }
        const GlobalSettings previous = globalSettings;
        globalSettings = defaults;
        settingsLoadedFromNvs = true;
        applyGlobalSettings(previous);
    } else {
        RuntimeSettings defaults = makeDefaultRuntimeSettings();
        copySettingText(defaults.cameraUsername, ctx->settings.cameraUsername);
        copySettingText(defaults.cameraPassword, ctx->settings.cameraPassword);
        if (!saveRuntimeSettingsToNvs(*ctx, defaults)) {
            sendApiError(500, "nvs_write_failed", "Failed to restore camera defaults");
            return;
        }
        const RuntimeSettings previous = ctx->settings;
        ctx->settings = defaults;
        ctx->settingsLoadedFromNvs = true;
        applyRuntimeSettings(*ctx, previous);
    }
    settingsRevision++;
    settingsWriteCount++;
    lastSettingsResult = currentTimestamp() + " - defaults restored; credentials preserved";
    PersistentLog::record(PersistentLog::Event::SettingsReset, PersistentLog::Outcome::Success, globalScope ? "global_defaults" : "camera_defaults", ctx != nullptr ? ctx->camera.ip : IPAddress(), 0, 0, 0, settingsRevision, PersistentLog::FlagManual, PersistentLog::Level::Warning, true, 0, ctx != nullptr ? ctx->id : 0);
    sendJsonResponse(200, buildSettingsJson(ctx));
}

bool queueAdminAction(AdminAction action, uint32_t cameraId, String& error)
{
    if (currentAdminAction() != AdminAction::None) {
        error = "Another action is in progress";
        return false;
    }
    pendingAdminCameraId = cameraId;
    pendingAdminActionContextId = allocateLogContextId();
    pendingAdminAction = action;
    lastAdminActionResult = currentTimestamp() + " - action queued: " + adminActionName(action);
    return true;
}

void handleMergeCameras()
{
    String error;
    if (!authorizeSettingsMutation(error)) {
        sendApiError(403, "forbidden", error);
        return;
    }
    uint32_t primaryId = 0;
    uint32_t secondaryId = 0;
    if (!readUnsignedSettingArgument("primaryId", 1, UINT32_MAX, primaryId, error) || !readUnsignedSettingArgument("secondaryId", 1, UINT32_MAX, secondaryId, error)) {
        sendApiError(422, "invalid_camera", error);
        return;
    }
    if (httpServer.arg("confirmSettings") != "1") {
        sendApiError(422, "settings_confirmation_required", "Confirm that the settings of the primary registration prevail; the original record stays in NVS");
        return;
    }
    CameraIoGuard ioGuard;
    if (!ioGuard.acquired()) {
        sendApiError(503, "camera_busy", "Camera access busy");
        return;
    }
    if (!mergeCameraRecords(primaryId, secondaryId, true, error)) {
        sendApiError(409, "merge_failed", error);
        return;
    }
    PersistentLog::record(PersistentLog::Event::SettingsContext, PersistentLog::Outcome::Success, "camera_merge", IPAddress(), 0, 0, 0, secondaryId, PersistentLog::FlagManual, PersistentLog::Level::Warning, true, 0, primaryId);
    String json = "{\"ok\":true,\"primaryId\":";
    json += primaryId;
    json += ",\"secondaryId\":";
    json += secondaryId;
    json += ",\"message\":\"Registrations merged; primary settings preserved\"}";
    sendJsonResponse(200, json);
}

void handleForgetCamera()
{
    String error;
    if (!authorizeSettingsMutation(error)) {
        sendApiError(403, "forbidden", error);
        return;
    }
    uint32_t cameraId = 0;
    if (!readUnsignedSettingArgument("cameraId", 1, UINT32_MAX, cameraId, error)) {
        sendApiError(422, "invalid_camera", error);
        return;
    }
    if (httpServer.arg("confirmForget") != "1") {
        sendApiError(422, "confirmation_required", "Confirm the permanent removal of the selected registration");
        return;
    }
    CameraIoGuard ioGuard;
    if (!ioGuard.acquired()) {
        sendApiError(503, "camera_busy", "Camera access busy");
        return;
    }
    CameraContext* ctx = findCamera(cameraId);
    if (ctx == nullptr || !httpServer.hasArg("expectedIp") || httpServer.arg("expectedIp") != ctx->camera.ip.toString()) {
        sendApiError(409, "camera_changed", "The registration changed; refresh the list before removing");
        return;
    }
    if (!forgetCameraRecord(cameraId, error)) {
        sendApiError(409, "forget_failed", error);
        return;
    }
    PersistentLog::record(PersistentLog::Event::SettingsContext, PersistentLog::Outcome::Success, "camera_forget", IPAddress(), 0, 0, 0, cameraId, PersistentLog::FlagManual, PersistentLog::Level::Warning, true, 0, cameraId);
    String json = "{\"ok\":true,\"cameraId\":";
    json += cameraId;
    json += ",\"message\":\"Registration removed; ID reserved so it is not reused\"}";
    sendJsonResponse(200, json);
}

void handleAdminAction(AdminAction action)
{
    String error;
    if (!authorizeSettingsMutation(error)) {
        sendApiError(403, "forbidden", error);
        return;
    }
    const bool individual = action == AdminAction::HealthCheck || action == AdminAction::CameraReboot || action == AdminAction::ResetRebootLimiter;
    CameraContext* ctx = individual ? requestedCamera() : nullptr;
    if (individual && ctx == nullptr)
        return;
    if (action == AdminAction::RebootAllCameras && cameraCount == 0) {
        sendApiError(409, "no_cameras", "No camera registered");
        return;
    }
    if (!queueAdminAction(action, ctx != nullptr ? ctx->id : 0, error)) {
        sendApiError(409, "action_busy", error);
        return;
    }
    String json = "{\"ok\":true,\"queued\":\"";
    json += adminActionName(action);
    json += "\",\"cameraId\":";
    json += ctx != nullptr ? ctx->id : 0;
    json += ",\"message\":\"Action received and will be run by the main loop\"}";
    sendJsonResponse(202, json);
}

void handleClearPersistentLogs()
{
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    const AdminAction busyAction = currentAdminAction();

    if (busyAction != AdminAction::None) {
        sendApiError(409, "action_busy", String("Another action is in progress: ") + adminActionName(busyAction));
        return;
    }

    const uint32_t removedRecords = static_cast<uint32_t>(PersistentLog::count());
    const uint16_t logContextId = allocateLogContextId();
    const unsigned long clearStartedAtMs = millis();

    if (!PersistentLog::clear()) {
        sendApiError(500, "log_clear_failed", "Could not clear the persistent log storage");
        return;
    }

    PersistentLog::record(PersistentLog::Event::LogsCleared, PersistentLog::Outcome::Success, "manual_clear", IPAddress(), 0, 0, millis() - clearStartedAtMs, removedRecords, PersistentLog::FlagManual, PersistentLog::Level::Warning, true, logContextId);
    lastAdminActionResult = currentTimestamp() + " - persistent log history cleared";
    String json = "{\"ok\":true,\"remaining\":";
    json += PersistentLog::count();
    json += ",\"message\":\"Previous history removed; audit marker preserved\"}";
    sendJsonResponse(200, json);
}

void handleFactoryReset()
{
    String authorizationError;
    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    if (httpServer.arg("confirmReset") != "ERASE EVERYTHING") {
        sendApiError(422, "confirmation_required", "Type ERASE EVERYTHING to confirm the factory data reset");
        return;
    }

    if (!PersistentLog::ready() || espRestartRequestedAtMs != 0) {
        sendApiError(409, "reset_unavailable", "Log storage unavailable or restart already scheduled");
        return;
    }

    factoryResetRequestedAtMs = millis();
    factoryResetFailed = false;
    lastAdminActionResult = "Factory data reset scheduled";
    sendJsonResponse(202, "{\"ok\":true,\"message\":\"Data reset scheduled; the ESP32 will restart\"}");
}

void handleGotoPtzPresetAction()
{
    CameraContext* selected = requestedCamera();
    if (selected == nullptr)
        return;
    CameraContext& ctx = *selected;
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    if (!httpServer.hasArg("presetToken")) {
        sendApiError(422, "missing_preset", "Preset token missing");
        return;
    }

    String presetToken = httpServer.arg("presetToken");
    presetToken.trim();

    if (presetToken.isEmpty() || presetToken.length() > 128) {
        sendApiError(422, "invalid_preset", "Invalid preset token");
        return;
    }

    if (!ctx.onvifPtz.presetsLoaded || onvifPtzPresetIndex(ctx, presetToken) < 0) {
        sendApiError(404, "preset_not_found", "Preset not found in the current camera catalog");
        return;
    }

    String queueError;

    if (!queueAdminAction(AdminAction::GotoPtzPreset, ctx.id, queueError)) {
        sendApiError(409, "action_busy", queueError);
        return;
    }

    pendingPtzPresetToken = presetToken;
    String json = "{\"ok\":true,\"queued\":\"goto_ptz_preset\",";
    json += "\"presetToken\":\"";
    json += jsonEscape(presetToken);
    json += "\",\"message\":\"PTZ movement received and will be run by the main loop\"}";
    sendJsonResponse(202, json);
}

const char* liveStreamErrorDetail(const char* code)
{
    if (strcmp(code, "camera_unavailable") == 0)
        return "The camera is not available right now";
    if (strcmp(code, "onvif_unavailable") == 0 || strcmp(code, "onvif_media_unavailable") == 0 || strcmp(code, "onvif_stream_uri_failed") == 0)
        return "The camera did not respond to the ONVIF media service";
    if (strcmp(code, "no_h264_profile") == 0)
        return "The camera does not offer an H.264 video profile";
    if (strcmp(code, "invalid_stream_uri") == 0)
        return "The camera announced an invalid video address";
    return "Could not prepare the live video";
}

void appendLiveStreamJson(String& json)
{
    const LiveStreamStats stats = liveStreamStats();
    json += "{\"state\":\"";
    json += liveStreamStateName(stats.state);
    json += "\",\"cameraId\":";
    json += stats.cameraId;
    json += ",\"viewers\":";
    json += stats.clients;
    json += ",\"profile\":\"";
    appendJsonEscaped(json, String(stats.profileName));
    json += "\",\"width\":";
    json += stats.width;
    json += ",\"height\":";
    json += stats.height;
    json += ",\"framesPerSecond\":";
    json += stats.framesPerSecond;
    json += ",\"kilobitsPerSecond\":";
    json += stats.kilobitsPerSecond;
    json += ",\"sessionsStarted\":";
    json += stats.sessionsStarted;
    json += ",\"sessionFailures\":";
    json += stats.sessionFailures;
    json += ",\"framesRelayed\":";
    json += stats.framesRelayed;
    json += ",\"packetsRelayed\":";
    json += stats.packetsRelayed;
    json += ",\"packetsDropped\":";
    json += stats.packetsDropped;
    json += ",\"viewersServed\":";
    json += stats.clientsServed;
    json += ",\"viewersRejected\":";
    json += stats.clientsRejected;
    json += ",\"bytesFromCamera\":";
    appendUnsigned64(json, stats.bytesFromCamera);
    json += ",\"bytesToViewers\":";
    appendUnsigned64(json, stats.bytesToClients);
    json += ",\"lastFrameAgeMs\":";
    json += stats.lastFrameAgeMs == UINT32_MAX ? -1L : static_cast<long>(stats.lastFrameAgeMs);
    json += ",\"sessionAgeSeconds\":";
    json += stats.sessionAgeMs / 1000UL;
    json += ",\"lastError\":\"";
    json += stats.lastError;
    json += "\",\"port\":";
    json += LIVE_STREAM_PORT;
    json += "}";
}

void handleLiveStart()
{
    CameraContext* ctx = requestedCamera();
    if (ctx == nullptr)
        return;

    const bool highQuality = httpServer.arg("quality") == "high";
    LiveStreamTarget target;
    bool haveTarget = liveStreamCopyTarget(target);
    const bool reusable = haveTarget && target.cameraId == ctx->id && target.highQuality == highQuality && ctx->camera.valid && target.ip[0] == ctx->camera.ip[0] && target.ip[1] == ctx->camera.ip[1] && target.ip[2] == ctx->camera.ip[2] && target.ip[3] == ctx->camera.ip[3] && millis() - target.resolvedAtMs < 10UL * 60UL * 1000UL;

    if (!reusable) {
        const LiveStreamStats stats = liveStreamStats();
        if (haveTarget && stats.clients > 0 && stats.state != LiveStreamState::Idle) {
            sendApiError(409, "live_in_use", "Another viewer is using the live video of another camera or quality");
            return;
        }
        if (healthCheckInProgress || registryBusy || cameraIoDepth != 0) {
            sendApiError(503, "busy", "Camera check in progress; try again");
            return;
        }
        CameraIoGuard cameraIoGuard;
        if (!cameraIoGuard.acquired()) {
            sendApiError(503, "busy", "Camera channel busy; try again");
            return;
        }
        LiveStreamTarget resolved;
        const char* error = "";
        if (!resolveLiveStreamTarget(*ctx, highQuality, resolved, error)) {
            sendApiError(503, error, liveStreamErrorDetail(error));
            return;
        }
        liveStreamSetTarget(resolved);
        target = resolved;
    }

    if (!liveStreamBegin()) {
        sendApiError(503, "relay_unavailable", "Could not start the video relay");
        return;
    }

    String json;
    json.reserve(220);
    json += "{\"ok\":true,\"port\":";
    json += LIVE_STREAM_PORT;
    json += ",\"cameraId\":";
    json += target.cameraId;
    json += ",\"profile\":\"";
    appendJsonEscaped(json, String(target.profileName));
    json += "\",\"width\":";
    json += target.width;
    json += ",\"height\":";
    json += target.height;
    json += ",\"quality\":\"";
    json += target.highQuality ? "high" : "fluid";
    json += "\"}";
    sendJsonResponse(200, json);
}

bool httpServerRunning = false;

void startHttpServer()
{
    if (httpServerRunning)
        return;
    httpServer.begin();
    httpServerRunning = true;
    Serial.printf("HTTP dashboard started on port %u.\n", HTTP_SERVER_PORT);
}

void configureHttpServer()
{
    httpServer.on("/", HTTP_GET, [] {
        httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        httpServer.sendHeader("Pragma", "no-cache");
        httpServer.send_P(200, "text/html; charset=utf-8", DASHBOARD_PAGE);
    });

    httpServer.on("/api/cameras", HTTP_GET, handleCameraRegistry);
    httpServer.on("/api/cameras/merge", HTTP_POST, handleMergeCameras);
    httpServer.on("/api/cameras/forget", HTTP_POST, handleForgetCamera);

    httpServer.on("/api/actions/reboot-all-cameras", HTTP_POST, [] {
        handleAdminAction(AdminAction::RebootAllCameras);
    });

    httpServer.on("/api/status", HTTP_GET, [] {
        CameraContext* ctx = requestedCamera();
        if (ctx != nullptr)
            sendJsonResponse(200, buildStatusJson(*ctx));
    });

    httpServer.on("/api/logs", HTTP_GET, [] {
        handlePersistentLogs();
    });

    httpServer.on("/api/logs/export", HTTP_GET, [] {
        handlePersistentLogsExport();
    });

    httpServer.on("/api/camera/snapshot", HTTP_GET, [] {
        CameraContext* ctx = requestedCamera();
        if (ctx != nullptr)
            serveCameraSnapshot(*ctx);
    });

    httpServer.on("/api/live/start", HTTP_GET, handleLiveStart);

    httpServer.on("/api/settings", HTTP_GET, [] {
        bool globalScope = false;
        if (!readSettingsScope(globalScope))
            return;
        CameraContext* ctx = globalScope ? nullptr : requestedCamera();
        if (globalScope || ctx != nullptr)
            sendJsonResponse(200, buildSettingsJson(ctx));
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

    httpServer.on("/api/actions/goto-ptz-preset", HTTP_POST, [] {
        handleGotoPtzPresetAction();
    });

    httpServer.on("/api/actions/reboot-camera", HTTP_POST, [] {
        handleAdminAction(AdminAction::CameraReboot);
    });

    httpServer.on("/api/actions/reset-reboot-limiter", HTTP_POST, [] {
        handleAdminAction(AdminAction::ResetRebootLimiter);
    });

    httpServer.on("/api/actions/clear-logs", HTTP_POST, [] {
        handleClearPersistentLogs();
    });

    httpServer.on("/api/actions/factory-reset", HTTP_POST, [] {
        handleFactoryReset();
    });

    httpServer.on("/api/actions/reboot-esp32", HTTP_POST, [] {
        handleAdminAction(AdminAction::RestartEsp32);
    });

    httpServer.onNotFound([] {
        if (wifiSetupHandleNotFound())
            return;
        httpServer.send(404, "application/json", "{\"error\":\"not_found\"}");
    });

    startHttpServer();
}

}
