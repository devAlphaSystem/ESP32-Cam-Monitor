#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

constexpr size_t LIVE_STREAM_URI_MAX_LENGTH = 512;
constexpr size_t LIVE_STREAM_PROFILE_NAME_MAX_LENGTH = 64;
constexpr size_t LIVE_STREAM_PROFILE_TOKEN_MAX_LENGTH = 128;
constexpr size_t LIVE_STREAM_ENCODING_MAX_LENGTH = 16;
constexpr size_t LIVE_RELAY_ERROR_MAX_LENGTH = 160;
constexpr size_t LIVE_RELAY_SYNC_RESULT_MAX_LENGTH = 160;
constexpr size_t LIVE_MEDIA_PROBE_DETAIL_MAX_LENGTH = 160;

struct LiveStreamDescriptor {
    char uri[LIVE_STREAM_URI_MAX_LENGTH + 1] = {0};
    char profileName[LIVE_STREAM_PROFILE_NAME_MAX_LENGTH + 1] = {0};
    char profileToken[LIVE_STREAM_PROFILE_TOKEN_MAX_LENGTH + 1] = {0};
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
    uint32_t syncAttempts = 0;
    uint32_t syncAccepted = 0;
    uint32_t syncFailed = 0;
    uint32_t syncSkipped = 0;
    uint32_t lastSyncAtMs = 0;
    bool lastSyncAccepted = false;
    char lastError[LIVE_RELAY_ERROR_MAX_LENGTH + 1] = {0};
    char lastSyncResult[LIVE_RELAY_SYNC_RESULT_MAX_LENGTH + 1] = {0};
};

struct LiveMediaProbeResult {
    bool attempted = false;
    bool descriptorResolved = false;
    bool rtspResponded = false;
    bool playAccepted = false;
    bool mediaReceived = false;
    int statusCode = 0;
    uint32_t latencyMs = 0;
    uint32_t observedAfterPlayMs = 0;
    uint32_t bytes = 0;
    char detail[LIVE_MEDIA_PROBE_DETAIL_MAX_LENGTH + 1] = {0};
};

using LiveStreamDescriptorProvider = bool (*)(
    size_t profileIndex,
    LiveStreamDescriptor& descriptor,
    String& error,
    uint32_t timeoutMs
);

using LiveStreamSynchronizationRequester = bool (*)(
    const char* profileToken,
    String& result
);

class LiveStreamRelay {
public:
    LiveStreamRelay();

    bool begin(
        uint16_t port,
        LiveStreamDescriptorProvider descriptorProvider,
        LiveStreamSynchronizationRequester synchronizationRequester,
        SemaphoreHandle_t cameraIoMutex,
        const char* username,
        const char* password,
        const char* userAgent
    );

    void snapshotStats(LiveRelayStats& output) const;
    void invalidateMediaEvidence();
    bool probeMedia(
        size_t profileIndex,
        uint32_t timeoutMs,
        LiveMediaProbeResult& result
    );

private:
    static void taskEntry(void* parameter);
    void run();
    void handleBrowserClient(WiFiClient browserClient);

    uint16_t port_ = 0;
    LiveStreamDescriptorProvider descriptorProvider_ = nullptr;
    LiveStreamSynchronizationRequester synchronizationRequester_ = nullptr;
    SemaphoreHandle_t cameraIoMutex_ = nullptr;
    const char* username_ = nullptr;
    const char* password_ = nullptr;
    const char* userAgent_ = nullptr;
    WiFiServer* server_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
    uint32_t lastSyncAttemptAtMs_ = 0;
    bool hasSyncAttempt_ = false;
    bool lastSyncAccepted_ = false;

    mutable portMUX_TYPE statsMux_ = portMUX_INITIALIZER_UNLOCKED;
    LiveRelayStats stats_;
};
