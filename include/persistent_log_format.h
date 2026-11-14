#pragma once

#include "persistent_log.h"

#include <stddef.h>

namespace PersistentLogFormat {

constexpr uint16_t RECORD_SCHEMA_VERSION = 4;
constexpr size_t STORED_CONTEXT_BYTES = 8;
constexpr size_t STORED_DETAIL_CAPACITY = PersistentLog::DETAIL_CAPACITY - STORED_CONTEXT_BYTES;

struct StoredRecord {
    uint32_t sequence;
    uint32_t epochSeconds;
    uint32_t uptimeMs;
    uint32_t targetIp;
    uint32_t durationMs;
    uint32_t value;
    uint16_t targetPort;
    int16_t statusCode;
    uint8_t event;
    uint8_t level;
    uint8_t outcome;
    uint8_t flags;
    uint8_t payload[PersistentLog::DETAIL_CAPACITY];
    uint32_t checksum;
};

static_assert(PersistentLog::DETAIL_CAPACITY > STORED_CONTEXT_BYTES + 1, "The persistent payload must reserve space for the detail");
static_assert(static_cast<uint16_t>(PersistentLog::Event::LiveSession) <= UINT8_MAX, "The persistent record stores the event in one byte");
static_assert(sizeof(StoredRecord) == 72, "The persistent record must be exactly 72 bytes");
static_assert(offsetof(StoredRecord, checksum) == 68, "The checksum must stay at the end of the persistent record");
static_assert(PersistentLog::CAPACITY * sizeof(StoredRecord) == 180000, "The circular file must leave headroom for LittleFS");

inline uint32_t checksumBytes(const void* values, size_t valueSize)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(values);
    uint32_t hash = 2166136261UL ^ RECORD_SCHEMA_VERSION;

    for (size_t index = 0; index < valueSize; index++) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }

    return hash;
}

inline uint32_t recordChecksum(const StoredRecord& record)
{
    return checksumBytes(&record, offsetof(StoredRecord, checksum));
}

inline bool validRecord(const StoredRecord& record)
{
    return record.checksum == recordChecksum(record);
}

inline uint32_t readUint32(const uint8_t* source)
{
    return static_cast<uint32_t>(source[0]) | (static_cast<uint32_t>(source[1]) << 8) | (static_cast<uint32_t>(source[2]) << 16) | (static_cast<uint32_t>(source[3]) << 24);
}

inline uint16_t readUint16(const uint8_t* source)
{
    return static_cast<uint16_t>(source[0]) | (static_cast<uint16_t>(source[1]) << 8);
}

}
