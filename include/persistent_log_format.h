#pragma once

#include "persistent_log.h"

#include <stddef.h>

namespace PersistentLogFormat {

constexpr uint32_t RECORD_MAGIC = 0x434C4F47UL;
constexpr uint16_t LEGACY_RECORD_SCHEMA_VERSION = 1;
constexpr uint16_t CONTEXT_RECORD_SCHEMA_VERSION = 2;
constexpr uint16_t RECORD_SCHEMA_VERSION = 3;
constexpr size_t STORED_CONTEXT_BYTES = 8;
constexpr size_t STORED_DETAIL_CAPACITY = PersistentLog::DETAIL_CAPACITY - STORED_CONTEXT_BYTES;

struct StoredRecord {
    uint32_t magic;
    uint32_t sequence;
    uint32_t epochSeconds;
    uint32_t uptimeMs;
    uint32_t targetIp;
    uint32_t durationMs;
    uint32_t value;
    uint16_t targetPort;
    int16_t statusCode;
    uint16_t event;
    uint16_t schemaVersion;
    uint8_t category;
    uint8_t level;
    uint8_t outcome;
    uint8_t flags;
    uint8_t payload[PersistentLog::DETAIL_CAPACITY];
    uint32_t checksum;
};

static_assert(PersistentLog::DETAIL_CAPACITY > STORED_CONTEXT_BYTES + 1, "O payload persistente precisa reservar espaco para o detalhe");
static_assert(sizeof(StoredRecord) == 80, "O registro persistente deve ocupar exatamente 80 bytes");
static_assert(offsetof(StoredRecord, checksum) == 76, "O checksum deve permanecer no final do registro persistente");
static_assert(PersistentLog::CAPACITY * sizeof(StoredRecord) == 600000, "O arquivo circular deve preservar folga para o LittleFS");

inline uint32_t checksumBytes(const void* values, size_t valueSize)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(values);
    uint32_t hash = 2166136261UL;

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
    return record.magic == RECORD_MAGIC && (record.schemaVersion == LEGACY_RECORD_SCHEMA_VERSION || record.schemaVersion == CONTEXT_RECORD_SCHEMA_VERSION || record.schemaVersion == RECORD_SCHEMA_VERSION) && record.checksum == recordChecksum(record);
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
