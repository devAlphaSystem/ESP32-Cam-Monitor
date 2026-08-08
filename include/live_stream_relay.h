#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

constexpr size_t LIVE_STREAM_URI_MAX_LENGTH = 512;
constexpr size_t LIVE_STREAM_PROFILE_NAME_MAX_LENGTH = 64;
constexpr size_t LIVE_STREAM_ENCODING_MAX_LENGTH = 16;
constexpr size_t LIVE_RELAY_ERROR_MAX_LENGTH = 160;

struct LiveStreamDescriptor {
    char uri[LIVE_STREAM_URI_MAX_LENGTH + 1] = {0};
    char profileName[LIVE_STREAM_PROFILE_NAME_MAX_LENGTH + 1] = {0};
    char encoding[LIVE_STREAM_ENCODING_MAX_LENGTH + 1] = {0};
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t declaredFps = 0;
    uint32_t declaredBitrateKbps = 0;
};

struct LiveRelayStats {
    bool running = false;
    bool clientActive = false;
    bool mediaActive = false;
    uint16_t port = 0;
    uint32_t sessions = 0;
    uint32_t failedSessions = 0;
    uint32_t packetsRelayed = 0;
    uint64_t bytesRelayed = 0;
    uint32_t oversizedPackets = 0;
    uint32_t lastSessionDurationSeconds = 0;
    uint32_t lastMediaPacketAtMs = 0;
    char lastError[LIVE_RELAY_ERROR_MAX_LENGTH + 1] = {0};
};

using LiveStreamDescriptorProvider = bool (*)(
    size_t profileIndex,
    LiveStreamDescriptor& descriptor,
    String& error
);

class LiveStreamRelay {
public:
    LiveStreamRelay();

    bool begin(
        uint16_t port,
        LiveStreamDescriptorProvider descriptorProvider,
        SemaphoreHandle_t cameraIoMutex,
        const char* username,
        const char* password,
        const char* userAgent
    );

    void snapshotStats(LiveRelayStats& output) const;

private:
    static void taskEntry(void* parameter);
    void run();
    void handleBrowserClient(WiFiClient browserClient);

    uint16_t port_ = 0;
    LiveStreamDescriptorProvider descriptorProvider_ = nullptr;
    SemaphoreHandle_t cameraIoMutex_ = nullptr;
    const char* username_ = nullptr;
    const char* password_ = nullptr;
    const char* userAgent_ = nullptr;
    WiFiServer* server_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;

    mutable portMUX_TYPE statsMux_ = portMUX_INITIALIZER_UNLOCKED;
    LiveRelayStats stats_;
};
