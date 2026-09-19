#include "runtime_settings.h"
#include "secrets.h"

#ifndef CAMERA_ONVIF_USERNAME_VALUE
#define CAMERA_ONVIF_USERNAME_VALUE ""
#endif

#ifndef CAMERA_ONVIF_PASSWORD_VALUE
#define CAMERA_ONVIF_PASSWORD_VALUE ""
#endif

namespace CameraWatchdog {

static_assert(sizeof(CAMERA_ONVIF_USERNAME_VALUE) - 1 <= CAMERA_USERNAME_MAX_LENGTH, "Usuario inicial da camera excede o limite");
static_assert(sizeof(CAMERA_ONVIF_PASSWORD_VALUE) - 1 <= CAMERA_PASSWORD_MAX_LENGTH, "Senha inicial da camera excede o limite");

RuntimeSettings makeDefaultRuntimeSettings()
{
    RuntimeSettings values{};
    copySettingText(values.cameraName, CAMERA_NAME);
    copySettingText(values.localHostname, LOCAL_HOSTNAME);
    copySettingText(values.cameraUsername, CAMERA_ONVIF_USERNAME_VALUE);
    copySettingText(values.cameraPassword, CAMERA_ONVIF_PASSWORD_VALUE);
    copySettingText(values.snapshotProfileMatch, CAMERA_SNAPSHOT_PROFILE_MATCH);
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
    values.maxOnvifRebootAttemptsPerWindow = MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW;

    values.wifiConnectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS;
    values.enableAutomaticSubnetScan = ENABLE_AUTOMATIC_SUBNET_SCAN ? 1 : 0;
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
    values.snapshotResolveRetryIntervalMs = SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS;
    values.snapshotFailuresBeforeReresolve = SNAPSHOT_FAILURES_BEFORE_RERESOLVE;

    values.enableCameraAlertLed = ENABLE_CAMERA_ALERT_LED ? 1 : 0;
    values.cameraAlertLedBlinkIntervalMs = CAMERA_ALERT_LED_BLINK_INTERVAL_MS;
    values.feedMinIntervalMs = FEED_MIN_INTERVAL_MS;
    values.feedTargetCycleMs = FEED_TARGET_CYCLE_MS;
    values.statusRefreshIntervalMs = STATUS_REFRESH_INTERVAL_MS;
    return values;
}

}
