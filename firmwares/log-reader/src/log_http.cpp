#include "log_http.h"

#include <WiFi.h>
#include <esp_err.h>
#include <esp_partition.h>
#include <stdlib.h>

#include "log_page.h"
#include "log_reader.h"

namespace LogHttp {
namespace {

constexpr size_t DEFAULT_PAGE_SIZE = 100;
constexpr size_t MAX_PAGE_SIZE = 250;
constexpr size_t RAW_CHUNK_SIZE = 1024;
constexpr uint32_t RAW_WRITE_STALL_TIMEOUT_MS = 10000UL;

WebServer* httpServer = nullptr;

struct Filters {
    String query;
    String level;
    String category;
    String event;
    String outcome;
    bool hasBootId = false;
    bool hasContextId = false;
    uint16_t bootId = 0;
    uint16_t contextId = 0;
};

size_t boundedNumberArgument(const char* name, size_t defaultValue, size_t maximum)
{
    if (!httpServer->hasArg(name)) {
        return defaultValue;
    }

    const String value = httpServer->arg(name);
    char* end = nullptr;
    const unsigned long parsed = strtoul(value.c_str(), &end, 10);

    if (end == value.c_str() || *end != '\0') {
        return defaultValue;
    }

    return min(static_cast<size_t>(parsed), maximum);
}

bool unsigned16Argument(const char* name, uint16_t& value)
{
    if (!httpServer->hasArg(name) || httpServer->arg(name).isEmpty()) {
        return false;
    }

    const String argument = httpServer->arg(name);
    char* end = nullptr;
    const unsigned long parsed = strtoul(argument.c_str(), &end, 10);

    if (end == argument.c_str() || *end != '\0' || parsed > UINT16_MAX) {
        return false;
    }

    value = static_cast<uint16_t>(parsed);
    return true;
}

String normalizedArgument(const char* name, size_t maximumLength)
{
    String value = httpServer->hasArg(name) ? httpServer->arg(name) : String();
    value.trim();

    if (value.length() > maximumLength) {
        value.remove(maximumLength);
    }

    value.toLowerCase();
    return value;
}

Filters readFilters()
{
    Filters filters;
    filters.query = normalizedArgument("q", 80);
    filters.level = normalizedArgument("level", 16);
    filters.category = normalizedArgument("category", 24);
    filters.event = normalizedArgument("event", 32);
    filters.outcome = normalizedArgument("outcome", 16);
    filters.hasBootId = unsigned16Argument("bootId", filters.bootId);
    filters.hasContextId = unsigned16Argument("contextId", filters.contextId);
    return filters;
}

bool containsQuery(const LogReader::Entry& entry, const String& query)
{
    if (query.isEmpty()) {
        return true;
    }

    String searchable;
    searchable.reserve(220);
    searchable += LogReader::levelName(entry.level);
    searchable += ' ';
    searchable += LogReader::categoryName(entry.category);
    searchable += ' ';
    searchable += LogReader::eventName(entry.event);
    searchable += ' ';
    searchable += LogReader::outcomeName(entry.outcome);
    searchable += ' ';
    searchable += entry.detail;
    searchable += ' ';
    searchable += LogReader::ipAddress(entry.targetIp);
    searchable += ' ';
    searchable += String(entry.sequence);
    searchable += ' ';
    searchable += String(entry.statusCode);
    searchable += ' ';
    searchable += String(entry.bootId);
    searchable += ' ';
    searchable += String(entry.contextId);
    searchable += ' ';
    searchable += String(entry.cameraId);
    searchable.toLowerCase();
    return searchable.indexOf(query) >= 0;
}

bool matchesFilters(const LogReader::Entry& entry, const Filters& filters)
{
    if (!filters.level.isEmpty() && filters.level != LogReader::levelName(entry.level)) {
        return false;
    }

    if (!filters.category.isEmpty() && filters.category != LogReader::categoryName(entry.category)) {
        return false;
    }

    if (!filters.event.isEmpty() && filters.event != LogReader::eventName(entry.event)) {
        return false;
    }

    if (!filters.outcome.isEmpty() && filters.outcome != LogReader::outcomeName(entry.outcome)) {
        return false;
    }

    if (filters.hasBootId && filters.bootId != entry.bootId) {
        return false;
    }

    if (filters.hasContextId && filters.contextId != entry.contextId) {
        return false;
    }

    return containsQuery(entry, filters.query);
}

void appendJsonString(String& destination, const char* value)
{
    destination += '"';

    if (value != nullptr) {
        const uint8_t* current = reinterpret_cast<const uint8_t*>(value);

        while (*current != 0) {
            const uint8_t character = *current++;

            switch (character) {
                case '"':
                    destination += F("\\\"");
                    break;
                case '\\':
                    destination += F("\\\\");
                    break;
                case '\b':
                    destination += F("\\b");
                    break;
                case '\f':
                    destination += F("\\f");
                    break;
                case '\n':
                    destination += F("\\n");
                    break;
                case '\r':
                    destination += F("\\r");
                    break;
                case '\t':
                    destination += F("\\t");
                    break;
                default:
                    if (character < 0x20) {
                        char escaped[7];
                        snprintf(escaped, sizeof(escaped), "\\u%04X", character);
                        destination += escaped;
                    } else {
                        destination += static_cast<char>(character);
                    }
            }
        }
    }

    destination += '"';
}

String entryJson(const LogReader::Entry& entry)
{
    String json;
    json.reserve(560);
    json += F("{\"slot\":");
    json += String(entry.slot);
    json += F(",\"sequence\":");
    json += String(entry.sequence);
    json += F(",\"schemaVersion\":");
    json += String(entry.schemaVersion);
    json += F(",\"epochSeconds\":");
    json += String(entry.epochSeconds);
    json += F(",\"uptimeMs\":");
    json += String(entry.uptimeMs);
    json += F(",\"bootId\":");
    json += String(entry.bootId);
    json += F(",\"contextId\":");
    json += String(entry.contextId);
    json += F(",\"cameraId\":");
    json += String(entry.cameraId);
    json += F(",\"level\":");
    appendJsonString(json, LogReader::levelName(entry.level));
    json += F(",\"levelCode\":");
    json += String(static_cast<unsigned int>(entry.level));
    json += F(",\"category\":");
    appendJsonString(json, LogReader::categoryName(entry.category));
    json += F(",\"categoryCode\":");
    json += String(static_cast<unsigned int>(entry.category));
    json += F(",\"event\":");
    appendJsonString(json, LogReader::eventName(entry.event));
    json += F(",\"eventCode\":");
    json += String(entry.event);
    json += F(",\"outcome\":");
    appendJsonString(json, LogReader::outcomeName(entry.outcome));
    json += F(",\"outcomeCode\":");
    json += String(static_cast<unsigned int>(entry.outcome));
    json += F(",\"flags\":");
    json += String(static_cast<unsigned int>(entry.flags));
    json += F(",\"targetIp\":");
    appendJsonString(json, LogReader::ipAddress(entry.targetIp).c_str());
    json += F(",\"targetIpValue\":");
    json += String(entry.targetIp);
    json += F(",\"targetPort\":");
    json += String(entry.targetPort);
    json += F(",\"statusCode\":");
    json += String(entry.statusCode);
    json += F(",\"durationMs\":");
    json += String(entry.durationMs);
    json += F(",\"value\":");
    json += String(entry.value);
    json += F(",\"detail\":");
    appendJsonString(json, entry.detail);
    json += '}';
    return json;
}

String csvField(const char* value)
{
    String field = "\"";

    if (value != nullptr) {
        if (*value == '=' || *value == '+' || *value == '-' || *value == '@') {
            field += '\'';
        }

        for (const char* current = value; *current != '\0'; current++) {
            if (*current == '"') {
                field += F("\"\"");
            } else if (*current == '\r' || *current == '\n') {
                field += ' ';
            } else {
                field += *current;
            }
        }
    }

    field += '"';
    return field;
}

String entryCsv(const LogReader::Entry& entry)
{
    String row;
    row.reserve(440);
    row += String(entry.slot) + ',' + String(entry.sequence) + ',' + String(entry.schemaVersion) + ',';
    row += String(entry.epochSeconds) + ',' + String(entry.uptimeMs) + ',' + String(entry.bootId) + ',' + String(entry.contextId) + ',' + String(entry.cameraId) + ',';
    row += csvField(LogReader::levelName(entry.level)) + ',' + String(static_cast<unsigned int>(entry.level)) + ',';
    row += csvField(LogReader::categoryName(entry.category)) + ',' + String(static_cast<unsigned int>(entry.category)) + ',';
    row += csvField(LogReader::eventName(entry.event)) + ',' + String(entry.event) + ',';
    row += csvField(LogReader::outcomeName(entry.outcome)) + ',' + String(static_cast<unsigned int>(entry.outcome)) + ',';
    row += String(static_cast<unsigned int>(entry.flags)) + ',' + csvField(LogReader::ipAddress(entry.targetIp).c_str()) + ',';
    row += String(entry.targetIp) + ',' + String(entry.targetPort) + ',' + String(entry.statusCode) + ',';
    row += String(entry.durationMs) + ',' + String(entry.value) + ',' + csvField(entry.detail) + "\r\n";
    return row;
}

void sendApiError(int code, const char* message)
{
    String json = F("{\"error\":");
    appendJsonString(json, message);
    json += '}';
    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->send(code, "application/json; charset=utf-8", json);
}

void handleStatus()
{
    const LogReader::Status& status = LogReader::status();
    const uint64_t chipId = ESP.getEfuseMac();
    char fallbackSsid[32];
    char fallbackPassword[16];
    snprintf(fallbackSsid, sizeof(fallbackSsid), "ESP32-LogReader-%06lX", static_cast<unsigned long>(chipId & 0xFFFFFFULL));
    snprintf(fallbackPassword, sizeof(fallbackPassword), "Logs-%06lX", static_cast<unsigned long>(chipId & 0xFFFFFFULL));
    String json;
    json.reserve(820);
    json += F("{\"mountAttempted\":");
    json += status.mountAttempted ? F("true") : F("false");
    json += F(",\"mountSkippedAfterReset\":");
    json += status.mountSkippedAfterReset ? F("true") : F("false");
    json += F(",\"mounted\":");
    json += status.mounted ? F("true") : F("false");
    json += F(",\"fileFound\":");
    json += status.fileFound ? F("true") : F("false");
    json += F(",\"mountError\":");
    json += String(status.mountError);
    json += F(",\"mountErrorName\":");
    appendJsonString(json, esp_err_to_name(status.mountError));
    json += F(",\"partitionAddress\":");
    json += String(status.partitionAddress);
    json += F(",\"partitionSize\":");
    json += String(status.partitionSize);
    json += F(",\"rawAvailable\":");
    json += LogReader::partition() != nullptr ? F("true") : F("false");
    json += F(",\"fileSize\":");
    json += String(status.fileSize);
    json += F(",\"recordSize\":");
    json += String(sizeof(PersistentLogFormat::StoredRecord));
    json += F(",\"slotCount\":");
    json += String(status.slotCount);
    json += F(",\"validCount\":");
    json += String(status.validCount);
    json += F(",\"invalidCount\":");
    json += String(status.invalidCount);
    json += F(",\"unknownSchemaCount\":");
    json += String(status.unknownSchemaCount);
    json += F(",\"partialBytes\":");
    json += String(status.partialBytes);
    json += F(",\"freeHeap\":");
    json += String(ESP.getFreeHeap());
    json += F(",\"stationConnected\":");
    json += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
    json += F(",\"stationIp\":");
    appendJsonString(json, WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
    json += F(",\"accessPointIp\":");
    appendJsonString(json, WiFi.getMode() == WIFI_MODE_APSTA || WiFi.getMode() == WIFI_MODE_AP ? WiFi.softAPIP().toString().c_str() : "");
    json += F(",\"fallbackAccessPointSsid\":");
    appendJsonString(json, fallbackSsid);
    json += F(",\"fallbackAccessPointPassword\":");
    appendJsonString(json, fallbackPassword);
    json += '}';

    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->send(200, "application/json; charset=utf-8", json);
}

void handleLogs()
{
    const LogReader::Status& status = LogReader::status();

    if (!status.fileFound) {
        sendApiError(503, "The event file is not available. Use the RAW export.");
        return;
    }

    const size_t offset = boundedNumberArgument("offset", 0, status.validCount);
    const size_t limit = max(static_cast<size_t>(1), boundedNumberArgument("limit", DEFAULT_PAGE_SIZE, MAX_PAGE_SIZE));
    const Filters filters = readFilters();
    uint16_t selected[MAX_PAGE_SIZE];
    size_t selectedCount = 0;
    size_t matchedCount = 0;

    for (size_t index = 0; index < status.validCount; index++) {
        LogReader::Entry entry;

        if (!LogReader::readNewest(index, entry) || !matchesFilters(entry, filters)) {
            continue;
        }

        if (matchedCount >= offset && selectedCount < limit) {
            selected[selectedCount++] = static_cast<uint16_t>(index);
        }

        matchedCount++;
    }

    String prefix;
    prefix.reserve(180);
    prefix += F("{\"total\":");
    prefix += String(matchedCount);
    prefix += F(",\"offset\":");
    prefix += String(offset);
    prefix += F(",\"limit\":");
    prefix += String(limit);
    prefix += F(",\"returned\":");
    prefix += String(selectedCount);
    prefix += F(",\"hasMore\":");
    prefix += offset + selectedCount < matchedCount ? F("true") : F("false");
    prefix += F(",\"entries\":[");

    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer->send(200, "application/json; charset=utf-8", "");
    httpServer->sendContent(prefix);

    for (size_t index = 0; index < selectedCount; index++) {
        LogReader::Entry entry;

        if (!LogReader::readNewest(selected[index], entry)) {
            continue;
        }

        if (index > 0) {
            httpServer->sendContent(",", 1);
        }

        httpServer->sendContent(entryJson(entry));
        delay(0);
    }

    httpServer->sendContent("]}", 2);
    httpServer->sendContent("");
}

void handleCsvExport()
{
    const LogReader::Status& status = LogReader::status();

    if (!status.fileFound) {
        sendApiError(503, "The event file is not available. Use the RAW export.");
        return;
    }

    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->sendHeader("Content-Disposition", "attachment; filename=watchdog-logs-full.csv");
    httpServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer->send(200, "text/csv; charset=utf-8", "");
    httpServer->sendContent("\xEF\xBB\xBF", 3);
    httpServer->sendContent("slot,sequence,schema_version,epoch_seconds,uptime_ms,boot_id,context_id,camera_id,level,level_code,category,category_code,event,event_code,outcome,outcome_code,flags,target_ip,target_ip_value,target_port,status_code,duration_ms,value,detail\r\n");

    for (size_t newestIndex = status.validCount; newestIndex > 0; newestIndex--) {
        LogReader::Entry entry;

        if (LogReader::readNewest(newestIndex - 1, entry)) {
            httpServer->sendContent(entryCsv(entry));
        }

        delay(0);
    }

    httpServer->sendContent("");
}

void handleJsonlExport()
{
    const LogReader::Status& status = LogReader::status();

    if (!status.fileFound) {
        sendApiError(503, "The event file is not available. Use the RAW export.");
        return;
    }

    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->sendHeader("Content-Disposition", "attachment; filename=watchdog-logs-full.jsonl");
    httpServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer->send(200, "application/x-ndjson; charset=utf-8", "");

    for (size_t newestIndex = status.validCount; newestIndex > 0; newestIndex--) {
        LogReader::Entry entry;

        if (LogReader::readNewest(newestIndex - 1, entry)) {
            String line = entryJson(entry);
            line += '\n';
            httpServer->sendContent(line);
        }

        delay(0);
    }

    httpServer->sendContent("");
}

bool writeAll(WiFiClient& client, const uint8_t* data, size_t length)
{
    size_t written = 0;
    uint32_t lastProgressAtMs = millis();

    while (written < length && client.connected()) {
        const size_t current = client.write(data + written, length - written);

        if (current == 0) {
            if (millis() - lastProgressAtMs >= RAW_WRITE_STALL_TIMEOUT_MS) {
                return false;
            }

            delay(1);
            continue;
        }

        written += current;
        lastProgressAtMs = millis();
        delay(0);
    }

    return written == length;
}

void handleRawExport()
{
    const esp_partition_t* partition = LogReader::partition();

    if (partition == nullptr) {
        sendApiError(404, "The logs partition was not found.");
        return;
    }

    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->sendHeader("Content-Disposition", "attachment; filename=watchdog-logs-partition.bin");
    httpServer->setContentLength(partition->size);
    httpServer->send(200, "application/octet-stream", "");

    WiFiClient client = httpServer->client();
    uint8_t buffer[RAW_CHUNK_SIZE];

    for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
        const size_t chunkSize = min(sizeof(buffer), static_cast<size_t>(partition->size - offset));

        if (esp_partition_read(partition, offset, buffer, chunkSize) != ESP_OK || !writeAll(client, buffer, chunkSize)) {
            Serial.println("RAW export stopped before the end.");
            return;
        }

        delay(0);
    }
}

void handleIndex()
{
    httpServer->sendHeader("Cache-Control", "no-store");
    httpServer->send_P(200, "text/html; charset=utf-8", LogPage::HTML);
}

}

void configure(WebServer& server)
{
    httpServer = &server;
    server.on("/", HTTP_GET, handleIndex);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/logs", HTTP_GET, handleLogs);
    server.on("/api/logs/export.csv", HTTP_GET, handleCsvExport);
    server.on("/api/logs/export.jsonl", HTTP_GET, handleJsonlExport);
    server.on("/api/logs/raw", HTTP_GET, handleRawExport);
    server.on("/health", HTTP_GET, []() {
        httpServer->sendHeader("Cache-Control", "no-store");
        httpServer->send(200, "text/plain; charset=utf-8", "OK");
    });
    server.onNotFound([]() {
        sendApiError(404, "Route not found.");
    });
}

}
