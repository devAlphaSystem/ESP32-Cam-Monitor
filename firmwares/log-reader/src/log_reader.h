#pragma once

#include <Arduino.h>
#include <esp_err.h>
#include <esp_partition.h>

#include "persistent_log_format.h"

namespace LogReader {

struct Entry {
    uint16_t slot = 0;
    uint16_t schemaVersion = 0;
    uint32_t sequence = 0;
    uint32_t epochSeconds = 0;
    uint32_t uptimeMs = 0;
    uint32_t targetIp = 0;
    uint32_t durationMs = 0;
    uint32_t value = 0;
    uint16_t bootId = 0;
    uint16_t contextId = 0;
    uint16_t targetPort = 0;
    int16_t statusCode = 0;
    uint16_t event = 0;
    uint8_t category = 0;
    uint8_t level = 0;
    uint8_t outcome = 0;
    uint8_t flags = 0;
    char detail[PersistentLog::DETAIL_CAPACITY + 1] = {0};
};

struct Status {
    bool mountAttempted = false;
    bool mountSkippedAfterReset = false;
    bool mounted = false;
    bool fileFound = false;
    esp_err_t mountError = ESP_OK;
    uint32_t partitionAddress = 0;
    uint32_t partitionSize = 0;
    uint32_t fileSize = 0;
    uint16_t partialBytes = 0;
    size_t slotCount = 0;
    size_t validCount = 0;
    size_t invalidCount = 0;
    size_t unknownSchemaCount = 0;
};

bool begin();
const Status& status();
const esp_partition_t* partition();
bool readNewest(size_t index, Entry& entry);

const char* levelName(uint8_t level);
const char* categoryName(uint8_t category);
const char* outcomeName(uint8_t outcome);
const char* eventName(uint16_t event);
String ipAddress(uint32_t value);

}
