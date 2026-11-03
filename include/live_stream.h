#pragma once

#include <Arduino.h>

namespace CameraWatchdog {

constexpr uint16_t LIVE_STREAM_PORT = 81;
constexpr size_t LIVE_STREAM_MAX_CLIENTS = 2;
constexpr size_t LIVE_STREAM_PATH_CAPACITY = 256;
constexpr size_t LIVE_STREAM_PROFILE_NAME_CAPACITY = 33;
constexpr size_t LIVE_STREAM_USERNAME_CAPACITY = 65;
constexpr size_t LIVE_STREAM_PASSWORD_CAPACITY = 129;

struct LiveStreamTarget {
    bool valid = false;
    uint32_t cameraId = 0;
    uint8_t ip[4] = {0, 0, 0, 0};
    uint16_t port = 554;
    uint16_t width = 0;
    uint16_t height = 0;
    bool highQuality = false;
    unsigned long resolvedAtMs = 0;
    char path[LIVE_STREAM_PATH_CAPACITY] = {0};
    char profileName[LIVE_STREAM_PROFILE_NAME_CAPACITY] = {0};
    char username[LIVE_STREAM_USERNAME_CAPACITY] = {0};
    char password[LIVE_STREAM_PASSWORD_CAPACITY] = {0};
};

enum class LiveStreamState : uint8_t {
    Idle,
    Connecting,
    Streaming,
    Retrying,
    Failed
};

struct LiveStreamStats {
    LiveStreamState state = LiveStreamState::Idle;
    uint32_t cameraId = 0;
    uint8_t clients = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t framesPerSecond = 0;
    uint32_t kilobitsPerSecond = 0;
    uint32_t sessionsStarted = 0;
    uint32_t sessionFailures = 0;
    uint32_t framesRelayed = 0;
    uint32_t packetsRelayed = 0;
    uint32_t packetsDropped = 0;
    uint32_t clientsServed = 0;
    uint32_t clientsRejected = 0;
    uint64_t bytesFromCamera = 0;
    uint64_t bytesToClients = 0;
    uint32_t lastFrameAgeMs = 0;
    uint32_t sessionAgeMs = 0;
    const char* lastError = "";
    char profileName[LIVE_STREAM_PROFILE_NAME_CAPACITY] = {0};
};

bool liveStreamBegin();

void liveStreamSetTarget(const LiveStreamTarget& target);

bool liveStreamCopyTarget(LiveStreamTarget& target);

LiveStreamStats liveStreamStats();

bool liveStreamDeliveredRecently(uint32_t cameraId, uint32_t windowMs, uint32_t* framesInWindow = nullptr);

const char* liveStreamStateName(LiveStreamState state);

}
