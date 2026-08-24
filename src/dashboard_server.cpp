#include "firmware_internal.h"

namespace CameraWatchdog {

void sendJsonResponse(int statusCode, const String& json);

void appendOnvifEndpointJson(String& json, const char* name, const OnvifServiceEndpoint& endpoint, bool prependComma)
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
    const SntpReferenceSnapshot sntpReference = snapshotSntpReference();
    const unsigned long now = millis();
    const bool sntpReferenceIsFresh = sntpReferenceFresh(sntpReference);

    String json;
    json.reserve(22000 + static_cast<size_t>(onvifPtz.presetCount) * 240);

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
    json += LOCAL_HOSTNAME;
    json += ".local\",\"localUrl\":\"http://";
    json += LOCAL_HOSTNAME;
    json += ".local/\",\"mdnsAvailable\":";
    json += mdnsAvailable && WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"gateway\":\"";
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
    json += onvifServices.resolvedAtMs == 0 ? 0UL : (now - onvifServices.resolvedAtMs) / 1000UL;
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
    json += ",\"eventsPullPoint\":";
    json += onvifServices.eventsPullPoint ? "true" : "false";
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
    json += "},\"ptz\":{";
    json += "\"available\":";
    json += onvifServices.ptz.available ? "true" : "false";
    json += ",\"profileResolved\":";
    json += onvifPtz.profileResolved ? "true" : "false";
    json += ",\"profileToken\":\"";
    json += jsonEscape(onvifPtz.profileToken);
    json += "\",\"profileName\":\"";
    json += jsonEscape(onvifPtz.profileName);
    json += "\",\"presetsLoaded\":";
    json += onvifPtz.presetsLoaded ? "true" : "false";
    json += ",\"presetCount\":";
    json += onvifPtz.presetCount;
    json += ",\"reportedPresetCount\":";
    json += onvifPtz.reportedPresetCount;
    json += ",\"truncated\":";
    json += onvifPtz.truncated ? "true" : "false";
    json += ",\"daemonPresetToken\":\"";
    json += jsonEscape(CAMERA_DAEMON_PRESET_TOKEN);
    json += "\",\"refreshAttempts\":";
    json += onvifPtz.refreshAttempts;
    json += ",\"refreshSuccesses\":";
    json += onvifPtz.refreshSuccesses;
    json += ",\"refreshFailures\":";
    json += onvifPtz.refreshFailures;
    json += ",\"lastStatus\":";
    json += onvifPtz.lastRefreshStatusCode;
    json += ",\"lastLatencyMs\":";
    json += onvifPtz.lastRefreshLatencyMs;
    json += ",\"ageSeconds\":";
    json += onvifPtz.refreshedAtMs == 0 ? 0UL : (now - onvifPtz.refreshedAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(onvifPtz.lastResult);
    json += "\",\"command\":{";
    json += "\"attempts\":";
    json += onvifPtz.commandAttempts;
    json += ",\"accepted\":";
    json += onvifPtz.commandAccepted;
    json += ",\"errors\":";
    json += onvifPtz.commandErrors;
    json += ",\"lastToken\":\"";
    json += jsonEscape(onvifPtz.lastCommandToken);
    json += "\",\"lastStatus\":";
    json += onvifPtz.lastCommandStatusCode;
    json += ",\"lastLatencyMs\":";
    json += onvifPtz.lastCommandLatencyMs;
    json += ",\"ageSeconds\":";
    json += onvifPtz.lastCommandAtMs == 0 ? 0UL : (now - onvifPtz.lastCommandAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(onvifPtz.lastCommandResult);
    json += "\"},\"presets\":[";

    for (uint16_t index = 0; index < onvifPtz.presetCount; index++) {
        if (index > 0) {
            json += ',';
        }

        json += "{\"token\":\"";
        json += jsonEscape(onvifPtz.presets[index].token);
        json += "\",\"name\":\"";
        json += jsonEscape(onvifPtz.presets[index].name);
        json += "\",\"daemon\":";
        json += onvifPtzPresetIsDaemon(onvifPtz.presets[index].token) ? "true" : "false";
        json += '}';
    }

    json += "]},\"deviceInfo\":{";
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
    json += ",\"profileT\":";
    json += onvifMetadata.profileT ? "true" : "false";
    json += ",\"requests\":";
    json += onvifMetadata.requests;
    json += ",\"failures\":";
    json += onvifMetadata.failures;
    json += ",\"lastResult\":\"";
    json += jsonEscape(onvifMetadata.lastResult);
    json += "\"},\"clock\":{";
    json += "\"checked\":";
    json += cameraClockStatus.checked ? "true" : "false";
    json += ",\"utcValid\":";
    json += cameraClockStatus.utcValid ? "true" : "false";
    json += ",\"healthy\":";
    json += (cameraClockStatus.utcValid && sntpReferenceIsFresh && cameraClockStatus.driftMs >= -CAMERA_CLOCK_HEALTHY_DRIFT_MS && cameraClockStatus.driftMs <= CAMERA_CLOCK_HEALTHY_DRIFT_MS) ? "true" : "false";
    json += ",\"referenceSynchronized\":";
    json += sntpReferenceIsFresh ? "true" : "false";
    json += ",\"dateTimeType\":\"";
    json += jsonEscape(cameraClockStatus.dateTimeType);
    json += "\",\"reportedTimeZone\":\"";
    json += jsonEscape(cameraClockStatus.timeZone);
    json += "\",\"daylightSavings\":";
    json += cameraClockStatus.daylightSavings ? "true" : "false";
    json += ",\"driftMilliseconds\":";
    if (cameraClockStatus.utcValid) {
        json += cameraClockStatus.driftMs;
    } else {
        json += "null";
    }
    json += ",\"roundTripMs\":";
    if (cameraClockStatus.utcValid) {
        json += cameraClockStatus.roundTripMs;
    } else {
        json += "null";
    }
    json += ",\"checks\":";
    json += cameraClockStatus.checks;
    json += ",\"failures\":";
    json += cameraClockStatus.failures;
    json += ",\"lastCheckAgeSeconds\":";
    json += cameraClockStatus.lastCheckAtMs == 0 ? 0UL : (now - cameraClockStatus.lastCheckAtMs) / 1000UL;
    json += ",\"referenceAgeSeconds\":";
    json += !sntpReference.synchronized || sntpReference.synchronizedAtMs == 0 ? 0UL : (now - sntpReference.synchronizedAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(cameraClockStatus.lastResult);
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
    json += ",\"rtspAttempted\":";
    json += lastRtspProbeAttempted ? "true" : "false";
    json += ",\"rtspProtocol\":";
    json += lastRtspProbe.protocolResponded ? "true" : "false";
    json += ",\"rtspStatus\":";
    json += lastRtspProbe.statusCode;
    json += ",\"rtspLatencyMs\":";
    json += lastRtspProbe.latencyMs;
    json += "},";

    json += "\"healthEvidence\":{\"source\":\"";
    json += healthEvidenceSourceName(lastHealthEvidenceSource);
    json += "\",\"ageSeconds\":";
    json += lastHealthEvidenceAtMs == 0 ? 0UL : (now - lastHealthEvidenceAtMs) / 1000UL;
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
    json += ",\"cameraHttpConnectionReuses\":";
    json += cameraJpegConnectionReuses;
    json += ",\"digestPreemptiveRequests\":";
    json += cameraJpegDigestPreemptiveRequests;
    json += ",\"previewFetchOperations\":";
    json += previewCameraFetches;
    json += ",\"busyResponses\":";
    json += previewBusyResponses;
    json += ",\"clientAborts\":";
    json += previewClientAborts;
    json += ",\"bytesTransferred\":";
    appendUnsigned64(json, previewBytesTransferred);
    json += ",\"healthEvidenceUses\":";
    json += previewHealthEvidenceUses;
    json += ",\"lastDeliveredAgeSeconds\":";
    json += lastBrowserSnapshotDeliveredAtMs == 0 ? 0UL : (now - lastBrowserSnapshotDeliveredAtMs) / 1000UL;
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
    json += incidentStartedAtMs == 0 ? 0UL : (now - incidentStartedAtMs) / 1000UL;
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
    json += ",\"automatic\":{";
    json += "\"attempts\":";
    json += automaticCameraRebootAttempts;
    json += ",\"accepted\":";
    json += automaticCameraRebootAccepted;
    json += ",\"errors\":";
    json += automaticCameraRebootErrors;
    json += "},\"manual\":{";
    json += "\"attempts\":";
    json += manualCameraRebootAttempts;
    json += ",\"accepted\":";
    json += manualCameraRebootAccepted;
    json += ",\"errors\":";
    json += manualCameraRebootErrors;
    json += "}";
    json += ",\"attemptsInWindow\":";
    json += cameraRebootAttemptsInWindow;
    json += ",\"maxAttemptsInWindow\":";
    json += MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW;
    json += ",\"lastAttemptAgeSeconds\":";
    json += lastCameraRebootAttemptAtMs == 0 ? 0UL : (now - lastCameraRebootAttemptAtMs) / 1000UL;
    json += ",\"lastResult\":\"";
    json += jsonEscape(lastCameraRebootResult);
    json += "\",\"lastEvidence\":\"";
    json += jsonEscape(lastCameraRebootEvidence);
    json += "\",\"acceptedHistory\":[";

    const size_t cameraRebootHistoryStart = (cameraRebootHistoryNext + CAMERA_REBOOT_HISTORY_CAPACITY - cameraRebootHistoryCount) % CAMERA_REBOOT_HISTORY_CAPACITY;

    for (size_t offset = 0; offset < cameraRebootHistoryCount; offset++) {
        if (offset > 0) {
            json += ',';
        }

        const CameraRebootEvidence& evidence = cameraRebootHistory[(cameraRebootHistoryStart + offset) % CAMERA_REBOOT_HISTORY_CAPACITY];
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
    json += String(1000.0f / FEED_TARGET_CYCLE_MS, 2);
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
        case PersistentLog::Event::TcpScan:
        case PersistentLog::Event::OnvifProbe:
        case PersistentLog::Event::RtspProbe:
        case PersistentLog::Event::SettingsContext:
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

    for (size_t index = offset; index < total && returned < limit; index++) {
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
    json += ",\"hasMore\":";
    json += offset + returned < total ? "true" : "false";
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
    chunk += "boot_id;context_id;timestamp_utc;time_synchronized;timeout_ms\r\n";

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
    if (blockDuringHealthCheck && healthCheckInProgress) {
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

    if (adminPasswordConfigured() && (!httpServer.hasArg("adminPassword") || !secureTextEquals(httpServer.arg("adminPassword"), DASHBOARD_ADMIN_PASSWORD_VALUE))) {
        error = "Senha administrativa incorreta";
        return false;
    }

    return true;
}

bool authorizeSettingsMutation(String& error)
{
    return authorizeDashboardMutation(error, true);
}

bool readStringSettingArgument(const char* name, size_t minimumLength, size_t maximumLength, String& value, String& error)
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

bool readUnsignedSettingArgument(const char* name, uint32_t minimum, uint32_t maximum, uint32_t& value, String& error)
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

bool readBooleanSettingArgument(const char* name, uint8_t& value, String& error)
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

bool parseRuntimeSettingsRequest(RuntimeSettings& candidate, String& error)
{
    String textValue;
    uint32_t numericValue = 0;

    if (!readStringSettingArgument("cameraName", CAMERA_NAME_MIN_LENGTH, CAMERA_NAME_MAX_LENGTH, textValue, error)) {
        return false;
    }
    copySettingText(candidate.cameraName, textValue);

    if (!readStringSettingArgument("localHostname", LOCAL_HOSTNAME_MIN_LENGTH, LOCAL_HOSTNAME_MAX_LENGTH, textValue, error)) {
        return false;
    }
    copySettingText(candidate.localHostname, textValue);

    if (!readStringSettingArgument("snapshotProfileMatch", 0, SNAPSHOT_PROFILE_MATCH_MAX_LENGTH, textValue, error)) {
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

    READ_U32(wifiConnectTimeoutMs, "wifiConnectTimeoutMs", WIFI_CONNECT_TIMEOUT_MIN_MS, WIFI_CONNECT_TIMEOUT_MAX_MS);
    READ_BOOL(enableAutomaticSubnetScan, "enableAutomaticSubnetScan");
    READ_U32(discoveryTimeoutMs, "discoveryTimeoutMs", DISCOVERY_TIMEOUT_MIN_MS, DISCOVERY_TIMEOUT_MAX_MS);
    READ_U32(rediscoveryIntervalMs, "rediscoveryIntervalMs", REDISCOVERY_INTERVAL_MIN_MS, REDISCOVERY_INTERVAL_MAX_MS);
    READ_U32(subnetScanIntervalMs, "subnetScanIntervalMs", SUBNET_SCAN_INTERVAL_MIN_MS, SUBNET_SCAN_INTERVAL_MAX_MS);

    READ_BOOL(enableEmailAlerts, "enableEmailAlerts");
    READ_BOOL(emailOnIpChange, "emailOnIpChange");
    READ_U32(emailRetryIntervalMs, "emailRetryIntervalMs", EMAIL_RETRY_INTERVAL_MIN_MS, EMAIL_RETRY_INTERVAL_MAX_MS);

    READ_U32(tcpConnectTimeoutMs, "tcpConnectTimeoutMs", TCP_CONNECT_TIMEOUT_MIN_MS, TCP_CONNECT_TIMEOUT_MAX_MS);
    READ_U32(serviceResponseTimeoutMs, "serviceResponseTimeoutMs", SERVICE_RESPONSE_TIMEOUT_MIN_MS, SERVICE_RESPONSE_TIMEOUT_MAX_MS);
    READ_U32(scanConnectTimeoutMs, "scanConnectTimeoutMs", SCAN_CONNECT_TIMEOUT_MIN_MS, SCAN_CONNECT_TIMEOUT_MAX_MS);
    READ_U32(snapshotHttpTimeoutMs, "snapshotHttpTimeoutMs", SNAPSHOT_HTTP_TIMEOUT_MIN_MS, SNAPSHOT_HTTP_TIMEOUT_MAX_MS);
    READ_U32(snapshotResolveRetryIntervalMs, "snapshotResolveRetryIntervalMs", SNAPSHOT_RESOLVE_RETRY_INTERVAL_MIN_MS, SNAPSHOT_RESOLVE_RETRY_INTERVAL_MAX_MS);
    READ_U8(snapshotFailuresBeforeReresolve, "snapshotFailuresBeforeReresolve", SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MIN, SNAPSHOT_FAILURES_BEFORE_RERESOLVE_MAX);

    READ_BOOL(enableCameraAlertLed, "enableCameraAlertLed");
    READ_U32(cameraAlertLedBlinkIntervalMs, "cameraAlertLedBlinkIntervalMs", CAMERA_ALERT_LED_BLINK_INTERVAL_MIN_MS, CAMERA_ALERT_LED_BLINK_INTERVAL_MAX_MS);
    READ_U32(feedMinIntervalMs, "feedMinIntervalMs", FEED_MIN_INTERVAL_MIN_MS, FEED_MIN_INTERVAL_MAX_MS);
    READ_U32(feedTargetCycleMs, "feedTargetCycleMs", FEED_TARGET_CYCLE_MIN_MS, FEED_TARGET_CYCLE_MAX_MS);
    READ_U32(statusRefreshIntervalMs, "statusRefreshIntervalMs", STATUS_REFRESH_INTERVAL_MIN_MS, STATUS_REFRESH_INTERVAL_MAX_MS);
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
    json += (static_cast<uint64_t>(HEALTH_CHECK_INTERVAL_MS) * VIDEO_FAILURES_BEFORE_ALERT) / 1000ULL;
    json += ",\"imageRebootSeconds\":";
    json += (static_cast<uint64_t>(HEALTH_CHECK_INTERVAL_MS) * VIDEO_FAILURES_BEFORE_REBOOT) / 1000ULL;
    json += ",\"offlineSeconds\":";
    json += (static_cast<uint64_t>(HEALTH_CHECK_INTERVAL_MS) * FAILURES_BEFORE_OFFLINE) / 1000ULL;
    json += "},\"values\":{";
    json += "\"cameraName\":\"";
    json += jsonEscape(CAMERA_NAME);
    json += "\",\"localHostname\":\"";
    json += jsonEscape(LOCAL_HOSTNAME);
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
    PersistentLog::record(PersistentLog::Event::SettingsSaved, PersistentLog::Outcome::Success, "runtime_settings", IPAddress(), 0, 0, 0, settingsRevision, PersistentLog::FlagManual, PersistentLog::Level::Info, true);
    recordRuntimeSettingsContext("nvs", PersistentLog::FlagManual);
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
    PersistentLog::record(PersistentLog::Event::SettingsReset, PersistentLog::Outcome::Success, "factory_defaults", IPAddress(), 0, 0, 0, settingsRevision, PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
    recordRuntimeSettingsContext("factory", PersistentLog::FlagManual);
    sendJsonResponse(200, buildSettingsJson());
}

bool queueAdminAction(AdminAction action, String& error)
{
    const AdminAction busyAction = currentAdminAction();

    if (busyAction != AdminAction::None) {
        error = String("Outra acao esta em andamento: ") + adminActionName(busyAction);
        return false;
    }

    pendingAdminAction = action;
    pendingAdminActionContextId = allocateLogContextId();
    lastAdminActionResult = currentTimestamp() + " - acao enfileirada: " + adminActionName(action);
    PersistentLog::record(PersistentLog::Event::AdminAction, PersistentLog::Outcome::Started, adminActionName(action), camera.ip, camera.onvifPort, 0, 0, static_cast<uint32_t>(action), PersistentLog::FlagManual, PersistentLog::Level::Info, true, pendingAdminActionContextId);
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

void handleClearPersistentLogs()
{
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    const AdminAction busyAction = currentAdminAction();

    if (busyAction != AdminAction::None) {
        sendApiError(409, "action_busy", String("Outra acao esta em andamento: ") + adminActionName(busyAction));
        return;
    }

    const uint32_t removedRecords = static_cast<uint32_t>(PersistentLog::count());
    const uint16_t logContextId = allocateLogContextId();
    const unsigned long clearStartedAtMs = millis();

    if (!PersistentLog::clear()) {
        sendApiError(500, "log_clear_failed", "Nao foi possivel zerar o armazenamento persistente de logs");
        return;
    }

    PersistentLog::record(PersistentLog::Event::LogsCleared, PersistentLog::Outcome::Success, "manual_clear", IPAddress(), 0, 0, millis() - clearStartedAtMs, removedRecords, PersistentLog::FlagManual, PersistentLog::Level::Warning, true, logContextId);
    lastAdminActionResult = currentTimestamp() + " - historico persistente de logs zerado";
    String json = "{\"ok\":true,\"remaining\":";
    json += PersistentLog::count();
    json += ",\"message\":\"Historico anterior removido; marcador de auditoria preservado\"}";
    sendJsonResponse(200, json);
}

void handleGotoPtzPresetAction()
{
    String authorizationError;

    if (!authorizeSettingsMutation(authorizationError)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }

    if (!httpServer.hasArg("presetToken")) {
        sendApiError(422, "missing_preset", "Token do preset ausente");
        return;
    }

    String presetToken = httpServer.arg("presetToken");
    presetToken.trim();

    if (presetToken.isEmpty() || presetToken.length() > 128) {
        sendApiError(422, "invalid_preset", "Token do preset invalido");
        return;
    }

    if (!onvifPtz.presetsLoaded || onvifPtzPresetIndex(presetToken) < 0) {
        sendApiError(404, "preset_not_found", "Preset nao encontrado no catalogo atual da camera");
        return;
    }

    String queueError;

    if (!queueAdminAction(AdminAction::GotoPtzPreset, queueError)) {
        sendApiError(409, "action_busy", queueError);
        return;
    }

    pendingPtzPresetToken = presetToken;
    String json = "{\"ok\":true,\"queued\":\"goto_ptz_preset\",";
    json += "\"presetToken\":\"";
    json += jsonEscape(presetToken);
    json += "\",\"message\":\"Movimento PTZ recebido e sera executado pelo loop principal\"}";
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
        httpServer.send(200, "application/json; charset=utf-8", buildStatusJson());
    });

    httpServer.on("/api/logs", HTTP_GET, [] {
        handlePersistentLogs();
    });

    httpServer.on("/api/logs/export", HTTP_GET, [] {
        handlePersistentLogsExport();
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

}
