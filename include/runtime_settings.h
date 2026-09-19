#pragma once

#include <Arduino.h>

#include "app_config.h"

namespace CameraWatchdog {

struct GlobalSettings {
    uint32_t wifiConnectTimeoutMs;
    uint8_t enableAutomaticSubnetScan;
    uint32_t discoveryTimeoutMs;
    uint32_t rediscoveryIntervalMs;
    uint32_t subnetScanIntervalMs;
    uint32_t emailRetryIntervalMs;
    uint32_t scanConnectTimeoutMs;
    uint8_t enableCameraAlertLed;
    uint32_t cameraAlertLedBlinkIntervalMs;
    uint32_t statusRefreshIntervalMs;
    char localHostname[LOCAL_HOSTNAME_MAX_LENGTH + 1];
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
    uint8_t enableEmailAlerts;
    uint8_t emailOnIpChange;
    uint32_t tcpConnectTimeoutMs;
    uint32_t serviceResponseTimeoutMs;
    uint32_t snapshotHttpTimeoutMs;
    uint32_t snapshotResolveRetryIntervalMs;
    uint8_t snapshotFailuresBeforeReresolve;
    uint32_t feedMinIntervalMs;
    uint32_t feedTargetCycleMs;
    char cameraUsername[CAMERA_USERNAME_MAX_LENGTH + 1];
    char cameraPassword[CAMERA_PASSWORD_MAX_LENGTH + 1];
};

static_assert(sizeof(RuntimeSettings) <= 512, "RuntimeSettings excedeu o limite planejado");

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
    const size_t copyLength = min(static_cast<size_t>(source.length()), Size - 1);
    memcpy(destination, source.c_str(), copyLength);
    destination[copyLength] = '\0';
}

GlobalSettings makeDefaultGlobalSettings();
RuntimeSettings makeDefaultRuntimeSettings(bool initialCredentials = false);

}
