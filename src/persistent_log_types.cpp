#include "persistent_log.h"

namespace PersistentLog {

const char* levelName(Level level)
{
    switch (level) {
        case Level::Info:
            return "info";
        case Level::Warning:
            return "warning";
        case Level::Error:
            return "error";
        case Level::Automatic:
            return "unknown";
    }

    return "unknown";
}

const char* categoryName(Category category)
{
    switch (category) {
        case Category::System:
            return "system";
        case Category::Wifi:
            return "wifi";
        case Category::Camera:
            return "camera";
        case Category::Discovery:
            return "discovery";
        case Category::Onvif:
            return "onvif";
        case Category::Snapshot:
            return "snapshot";
        case Category::Rtsp:
            return "rtsp";
        case Category::Health:
            return "health";
        case Category::Action:
            return "action";
        case Category::Notification:
            return "notification";
        case Category::Settings:
            return "settings";
    }

    return "unknown";
}

const char* outcomeName(Outcome outcome)
{
    switch (outcome) {
        case Outcome::Info:
            return "info";
        case Outcome::Started:
            return "started";
        case Outcome::Success:
            return "success";
        case Outcome::Failure:
            return "failure";
        case Outcome::Skipped:
            return "skipped";
    }

    return "unknown";
}

const char* eventName(Event event)
{
    switch (event) {
        case Event::SystemBoot:
            return "system_boot";
        case Event::SystemRestart:
            return "system_restart";
        case Event::WifiConnect:
            return "wifi_connect";
        case Event::WifiDisconnect:
            return "wifi_disconnect";
        case Event::CameraState:
            return "camera_state";
        case Event::Incident:
            return "incident";
        case Event::HealthCheck:
            return "health_check";
        case Event::DiscoveryProbe:
            return "discovery_probe";
        case Event::DiscoveryResult:
            return "discovery_result";
        case Event::OnvifProbe:
            return "onvif_probe";
        case Event::OnvifSoap:
            return "onvif_soap";
        case Event::SnapshotHttp:
            return "snapshot_http";
        case Event::SnapshotTransfer:
            return "snapshot_transfer";
        case Event::RtspProbe:
            return "rtsp_probe";
        case Event::EmailHttp:
            return "email_http";
        case Event::SettingsReset:
            return "settings_reset";
        case Event::AdminAction:
            return "admin_action";
        case Event::CameraReboot:
            return "camera_reboot";
        case Event::PtzCommand:
            return "ptz_command";
        case Event::SettingsContext:
            return "settings_context";
        case Event::DiscoveryResponse:
            return "discovery_response";
        case Event::LogsCleared:
            return "logs_cleared";
        case Event::LiveSession:
            return "live_session";
    }

    return "unknown";
}
}
