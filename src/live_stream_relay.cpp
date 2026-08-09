#include "live_stream_relay.h"
#include "app_config.h"

#include <MD5Builder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <memory>
#include <new>

namespace {

constexpr uint8_t LIVE_RECORD_STATUS = 1;
constexpr uint8_t LIVE_RECORD_RTP = 2;
constexpr uint8_t LIVE_RECORD_ERROR = 3;
constexpr uint32_t BROWSER_REQUEST_TIMEOUT_MS = 3000UL;
constexpr size_t MAX_BROWSER_REQUEST_BYTES = 2048;
constexpr size_t MAX_RTSP_HEADER_BYTES = 8192;
constexpr size_t MAX_RTSP_BODY_BYTES = 32768;
constexpr UBaseType_t LIVE_TASK_PRIORITY = 1;
constexpr BaseType_t LIVE_TASK_CORE = 0;
constexpr uint32_t LIVE_SYNC_SUCCESS_MIN_INTERVAL_MS = 5000UL;
constexpr uint32_t LIVE_SYNC_FAILURE_BACKOFF_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t LIVE_PROBE_TEARDOWN_RESERVE_MS = 25UL;

struct RtspDeadline {
    uint32_t startedAtMs = 0;
    uint32_t timeoutMs = 0;
};

uint32_t remainingTimeMs(const RtspDeadline* deadline)
{
    if (deadline == nullptr) {
        return UINT32_MAX;
    }

    const uint32_t elapsed = millis() - deadline->startedAtMs;
    return elapsed >= deadline->timeoutMs
        ? 0
        : deadline->timeoutMs - elapsed;
}

uint32_t boundedTimeoutMs(
    uint32_t normalTimeoutMs,
    const RtspDeadline* deadline
)
{
    const uint32_t remaining = remainingTimeMs(deadline);
    return remaining < normalTimeoutMs ? remaining : normalTimeoutMs;
}

struct ParsedRtspUri {
    bool valid = false;
    IPAddress ip;
    String host;
    uint16_t port = 554;
    String path;
    String requestUri;
    String authority;
};

enum class AuthKind {
    None,
    Basic,
    Digest
};

struct RtspAuthChallenge {
    AuthKind kind = AuthKind::None;
    bool valid = false;
    String realm;
    String nonce;
    String qop;
    String opaque;
    String algorithm = "MD5";
};

struct RtspResponse {
    int statusCode = 0;
    String headers;
    String body;
};

struct SdpVideoTrack {
    bool valid = false;
    uint8_t payloadType = 96;
    uint32_t clockRate = 90000;
    String codec;
    String control;
    String spropParameterSets;
};

struct RtspSession {
    WiFiClient cameraClient;
    ParsedRtspUri endpoint;
    RtspAuthChallenge challenge;
    uint32_t nonceCount = 0;
    uint32_t cseq = 1;
    String sessionId;
    String contentBase;
    String aggregateUri;
    String trackUri;
    uint8_t videoChannel = 0;
    SdpVideoTrack track;
};

struct RelaySessionResult {
    bool started = false;
    bool normalBrowserClose = false;
    uint32_t packets = 0;
    uint64_t bytes = 0;
    uint32_t oversizedPackets = 0;
    uint32_t durationSeconds = 0;
    String error;
};

String trimmed(String value)
{
    value.trim();
    return value;
}

String lowerCopy(String value)
{
    value.toLowerCase();
    return value;
}

void copyText(char* destination, size_t capacity, const String& source)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }

    const size_t length = min(
        static_cast<size_t>(source.length()),
        capacity - 1
    );
    memcpy(destination, source.c_str(), length);
    destination[length] = '\0';
}

void recordSynchronizationResult(
    LiveRelayStats& relayStats,
    portMUX_TYPE& statsMux,
    bool attempted,
    bool accepted,
    uint32_t recordedAtMs,
    const String& result
)
{
    portENTER_CRITICAL(&statsMux);

    if (attempted) {
        relayStats.syncAttempts++;
        relayStats.lastSyncAccepted = accepted;

        if (accepted) {
            relayStats.syncAccepted++;
        } else {
            relayStats.syncFailed++;
        }
    } else {
        relayStats.syncSkipped++;
    }

    relayStats.lastSyncAtMs = recordedAtMs;
    copyText(
        relayStats.lastSyncResult,
        sizeof(relayStats.lastSyncResult),
        result
    );
    portEXIT_CRITICAL(&statsMux);
}

void requestSynchronizationPoint(
    const LiveStreamDescriptor& descriptor,
    LiveStreamSynchronizationRequester synchronizationRequester,
    uint32_t& lastSyncAttemptAtMs,
    bool& hasSyncAttempt,
    bool& lastSyncAccepted,
    LiveRelayStats& relayStats,
    portMUX_TYPE& statsMux
)
{
    const uint32_t now = millis();
    String syncResult;

    if (descriptor.profileToken[0] == '\0') {
        syncResult = "Sincronizacao ONVIF ignorada: perfil sem token";
        recordSynchronizationResult(
            relayStats,
            statsMux,
            false,
            false,
            now,
            syncResult
        );
        Serial.print("LIVE: ");
        Serial.println(syncResult);
        return;
    }

    if (synchronizationRequester == nullptr) {
        syncResult = "Sincronizacao ONVIF ignorada: callback indisponivel";
        recordSynchronizationResult(
            relayStats,
            statsMux,
            false,
            false,
            now,
            syncResult
        );
        Serial.print("LIVE: ");
        Serial.println(syncResult);
        return;
    }

    const uint32_t retryIntervalMs = lastSyncAccepted
        ? LIVE_SYNC_SUCCESS_MIN_INTERVAL_MS
        : LIVE_SYNC_FAILURE_BACKOFF_MS;

    if (
        hasSyncAttempt &&
        now - lastSyncAttemptAtMs < retryIntervalMs
    ) {
        syncResult = lastSyncAccepted
            ? "Sincronizacao ONVIF ignorada: limite de 5 s apos sucesso"
            : "Sincronizacao ONVIF ignorada: backoff de 5 min apos falha";
        recordSynchronizationResult(
            relayStats,
            statsMux,
            false,
            false,
            now,
            syncResult
        );
        Serial.print("LIVE: ");
        Serial.println(syncResult);
        return;
    }

    lastSyncAttemptAtMs = now;
    hasSyncAttempt = true;
    const bool accepted = synchronizationRequester(
        descriptor.profileToken,
        syncResult
    );
    lastSyncAccepted = accepted;

    if (syncResult.isEmpty()) {
        syncResult = accepted
            ? "SetSynchronizationPoint aceito"
            : "SetSynchronizationPoint falhou";
    }

    recordSynchronizationResult(
        relayStats,
        statsMux,
        true,
        accepted,
        millis(),
        syncResult
    );

    Serial.print("LIVE: ");
    Serial.println(syncResult);
}

bool writeAll(
    WiFiClient& client,
    const uint8_t* data,
    size_t length,
    const RtspDeadline* deadline = nullptr
)
{
    size_t written = 0;
    unsigned long lastProgressAt = millis();

    while (written < length) {
        if (!client.connected() || remainingTimeMs(deadline) == 0) {
            return false;
        }

        const size_t current = client.write(data + written, length - written);

        if (current > 0) {
            written += current;
            lastProgressAt = millis();
            continue;
        }

        if (millis() - lastProgressAt >= LIVE_RTSP_RESPONSE_TIMEOUT_MS) {
            return false;
        }

        delay(1);
    }

    return true;
}

bool writeAll(
    WiFiClient& client,
    const String& text,
    const RtspDeadline* deadline = nullptr
)
{
    return writeAll(
        client,
        reinterpret_cast<const uint8_t*>(text.c_str()),
        text.length(),
        deadline
    );
}

bool readExact(
    WiFiClient& client,
    uint8_t* destination,
    size_t length,
    uint32_t timeoutMs,
    const RtspDeadline* deadline = nullptr
)
{
    size_t received = 0;
    unsigned long lastDataAt = millis();

    while (received < length) {
        if (remainingTimeMs(deadline) == 0) {
            return false;
        }

        const int available = client.available();

        if (available > 0) {
            const size_t amount = min(
                static_cast<size_t>(available),
                length - received
            );
            const int read = client.read(destination + received, amount);

            if (read > 0) {
                received += static_cast<size_t>(read);
                lastDataAt = millis();
                continue;
            }
        }

        if (
            !client.connected() ||
            remainingTimeMs(deadline) == 0 ||
            millis() - lastDataAt >= timeoutMs
        ) {
            return false;
        }

        delay(1);
    }

    return true;
}

bool discardExact(
    WiFiClient& client,
    size_t length,
    uint32_t timeoutMs,
    const RtspDeadline* deadline = nullptr
)
{
    uint8_t buffer[256];
    size_t remaining = length;

    while (remaining > 0) {
        const size_t amount = min(remaining, sizeof(buffer));

        if (!readExact(client, buffer, amount, timeoutMs, deadline)) {
            return false;
        }

        remaining -= amount;
    }

    return true;
}

bool readHeaders(
    WiFiClient& client,
    String& headers,
    uint32_t timeoutMs,
    char firstByte = '\0',
    const RtspDeadline* deadline = nullptr
)
{
    headers = String();
    headers.reserve(1024);

    if (firstByte != '\0') {
        headers += firstByte;
    }

    unsigned long lastDataAt = millis();

    while (headers.length() < MAX_RTSP_HEADER_BYTES) {
        if (remainingTimeMs(deadline) == 0) {
            return false;
        }

        while (client.available()) {
            if (remainingTimeMs(deadline) == 0) {
                return false;
            }

            const int value = client.read();

            if (value < 0) {
                break;
            }

            headers += static_cast<char>(value);
            lastDataAt = millis();

            if (headers.endsWith("\r\n\r\n")) {
                return true;
            }
        }

        if (
            !client.connected() ||
            remainingTimeMs(deadline) == 0 ||
            millis() - lastDataAt >= timeoutMs
        ) {
            return false;
        }

        delay(1);
    }

    return false;
}

String headerValue(const String& headers, const char* name)
{
    String normalizedName = String(name);
    normalizedName.toLowerCase();
    normalizedName += ':';

    int lineStart = 0;

    while (lineStart < static_cast<int>(headers.length())) {
        const int lineEnd = headers.indexOf("\r\n", lineStart);
        const int actualEnd = lineEnd >= 0
            ? lineEnd
            : static_cast<int>(headers.length());
        String line = headers.substring(lineStart, actualEnd);
        String normalizedLine = line;
        normalizedLine.toLowerCase();

        if (normalizedLine.startsWith(normalizedName)) {
            line = line.substring(normalizedName.length());
            line.trim();
            return line;
        }

        if (lineEnd < 0) {
            break;
        }

        lineStart = lineEnd + 2;
    }

    return String();
}

int parseRtspStatusCode(const String& headers)
{
    const int lineEnd = headers.indexOf("\r\n");
    const String line = lineEnd >= 0 ? headers.substring(0, lineEnd) : headers;

    if (!line.startsWith("RTSP/")) {
        return 0;
    }

    const int firstSpace = line.indexOf(' ');

    if (firstSpace < 0) {
        return 0;
    }

    return line.substring(firstSpace + 1).toInt();
}

bool readRtspResponse(
    WiFiClient& client,
    RtspResponse& response,
    char firstByte = '\0',
    const RtspDeadline* deadline = nullptr
)
{
    response = RtspResponse{};

    if (!readHeaders(
        client,
        response.headers,
        LIVE_RTSP_RESPONSE_TIMEOUT_MS,
        firstByte,
        deadline
    )) {
        return false;
    }

    response.statusCode = parseRtspStatusCode(response.headers);
    const long contentLength = headerValue(response.headers, "Content-Length").toInt();

    if (contentLength < 0 || contentLength > static_cast<long>(MAX_RTSP_BODY_BYTES)) {
        return false;
    }

    if (contentLength == 0) {
        return response.statusCode > 0;
    }

    response.body.reserve(static_cast<size_t>(contentLength) + 1);
    uint8_t bodyBuffer[512];
    size_t remaining = static_cast<size_t>(contentLength);

    while (remaining > 0) {
        const size_t amount = min(remaining, sizeof(bodyBuffer));

        if (!readExact(
            client,
            bodyBuffer,
            amount,
            LIVE_RTSP_RESPONSE_TIMEOUT_MS,
            deadline
        )) {
            response.body = String();
            return false;
        }

        response.body.concat(
            reinterpret_cast<const char*>(bodyBuffer),
            static_cast<unsigned int>(amount)
        );
        remaining -= amount;
    }

    return response.statusCode > 0;
}

bool parseRtspUri(const String& uri, ParsedRtspUri& parsed)
{
    parsed = ParsedRtspUri{};
    String normalized = uri;
    normalized.trim();

    if (!normalized.startsWith("rtsp://")) {
        return false;
    }

    const int authorityStart = 7;
    const int slashStart = normalized.indexOf('/', authorityStart);
    const int queryStart = normalized.indexOf('?', authorityStart);
    int suffixStart = -1;

    if (slashStart >= 0 && queryStart >= 0) {
        suffixStart = min(slashStart, queryStart);
    } else if (slashStart >= 0) {
        suffixStart = slashStart;
    } else if (queryStart >= 0) {
        suffixStart = queryStart;
    }

    const int authorityEnd = suffixStart >= 0
        ? suffixStart
        : static_cast<int>(normalized.length());
    String authority = normalized.substring(authorityStart, authorityEnd);
    const int userInfoEnd = authority.lastIndexOf('@');

    if (userInfoEnd >= 0) {
        authority = authority.substring(userInfoEnd + 1);
    }

    if (authority.isEmpty()) {
        return false;
    }

    String host = authority;
    uint16_t port = 554;
    const int colon = authority.lastIndexOf(':');

    if (colon > 0 && authority.indexOf(']') < 0) {
        const String portText = authority.substring(colon + 1);
        bool numeric = !portText.isEmpty();

        for (size_t index = 0; index < portText.length(); index++) {
            numeric = numeric && isDigit(portText.charAt(index));
        }

        if (!numeric) {
            return false;
        }

        const long value = portText.toInt();

        if (value <= 0 || value > 65535) {
            return false;
        }

        port = static_cast<uint16_t>(value);
        host = authority.substring(0, colon);
    }

    IPAddress ip;

    if (!ip.fromString(host.c_str())) {
        if (WiFi.hostByName(host.c_str(), ip) != 1) {
            return false;
        }
    }

    parsed.valid = true;
    parsed.ip = ip;
    parsed.host = host;
    parsed.port = port;
    parsed.path = suffixStart >= 0 ? normalized.substring(suffixStart) : "/";

    if (!parsed.path.startsWith("/")) {
        parsed.path = String("/") + parsed.path;
    }
    parsed.authority = host;

    if (port != 554) {
        parsed.authority += ':';
        parsed.authority += port;
    }

    parsed.requestUri = "rtsp://";
    parsed.requestUri += parsed.authority;
    parsed.requestUri += parsed.path;
    return true;
}

String extractAuthParameter(const String& challenge, const char* name)
{
    String normalized = challenge;
    normalized.toLowerCase();
    String marker = String(name);
    marker.toLowerCase();
    marker += '=';
    const int markerPosition = normalized.indexOf(marker);

    if (markerPosition < 0) {
        return String();
    }

    int valueStart = markerPosition + marker.length();

    while (
        valueStart < static_cast<int>(challenge.length()) &&
        isspace(static_cast<unsigned char>(challenge.charAt(valueStart)))
    ) {
        valueStart++;
    }

    if (valueStart >= static_cast<int>(challenge.length())) {
        return String();
    }

    if (challenge.charAt(valueStart) == '"') {
        valueStart++;
        const int valueEnd = challenge.indexOf('"', valueStart);
        return valueEnd >= 0
            ? challenge.substring(valueStart, valueEnd)
            : String();
    }

    int valueEnd = valueStart;

    while (
        valueEnd < static_cast<int>(challenge.length()) &&
        challenge.charAt(valueEnd) != ',' &&
        !isspace(static_cast<unsigned char>(challenge.charAt(valueEnd)))
    ) {
        valueEnd++;
    }

    return trimmed(challenge.substring(valueStart, valueEnd));
}

bool qopSupportsAuth(const String& qop)
{
    int start = 0;

    while (start < static_cast<int>(qop.length())) {
        const int comma = qop.indexOf(',', start);
        const int end = comma >= 0 ? comma : static_cast<int>(qop.length());
        String token = qop.substring(start, end);
        token.trim();
        token.toLowerCase();

        if (token == "auth") {
            return true;
        }

        if (comma < 0) {
            break;
        }

        start = comma + 1;
    }

    return false;
}

bool parseAuthChallenge(
    const String& authenticateHeader,
    RtspAuthChallenge& challenge
)
{
    challenge = RtspAuthChallenge{};
    String normalized = authenticateHeader;
    normalized.toLowerCase();
    const int digestPosition = normalized.indexOf("digest");
    const int basicPosition = normalized.indexOf("basic");

    if (digestPosition >= 0) {
        const String text = authenticateHeader.substring(digestPosition + 6);
        challenge.kind = AuthKind::Digest;
        challenge.realm = extractAuthParameter(text, "realm");
        challenge.nonce = extractAuthParameter(text, "nonce");
        challenge.qop = extractAuthParameter(text, "qop");
        challenge.opaque = extractAuthParameter(text, "opaque");
        const String algorithm = extractAuthParameter(text, "algorithm");

        if (!algorithm.isEmpty()) {
            challenge.algorithm = algorithm;
        }

        challenge.valid = !challenge.realm.isEmpty() && !challenge.nonce.isEmpty();
        return challenge.valid;
    }

    if (basicPosition >= 0) {
        challenge.kind = AuthKind::Basic;
        challenge.realm = extractAuthParameter(
            authenticateHeader.substring(basicPosition + 5),
            "realm"
        );
        challenge.valid = true;
        return true;
    }

    return false;
}

String md5Hex(const String& value)
{
    MD5Builder md5;
    md5.begin();
    md5.add(value);
    md5.calculate();
    return md5.toString();
}

String quotedEscape(const String& value)
{
    String output;
    output.reserve(value.length() + 8);

    for (size_t index = 0; index < value.length(); index++) {
        const char character = value.charAt(index);

        if (character == '\\' || character == '"') {
            output += '\\';
        }

        output += character;
    }

    return output;
}

String base64Encode(const String& value)
{
    const size_t capacity = 4 * ((value.length() + 2) / 3) + 1;
    std::unique_ptr<unsigned char[]> output(
        new (std::nothrow) unsigned char[capacity]
    );

    if (!output) {
        return String();
    }

    size_t written = 0;

    if (mbedtls_base64_encode(
        output.get(),
        capacity - 1,
        &written,
        reinterpret_cast<const unsigned char*>(value.c_str()),
        value.length()
    ) != 0) {
        return String();
    }

    output[written] = '\0';
    return String(reinterpret_cast<const char*>(output.get()));
}

String makeAuthorization(
    const char* method,
    const String& requestUri,
    const RtspAuthChallenge& challenge,
    const char* username,
    const char* password,
    uint32_t nonceCount
)
{
    if (!challenge.valid || username == nullptr || username[0] == '\0') {
        return String();
    }

    const String user = username;
    const String secret = password == nullptr ? String() : String(password);

    if (challenge.kind == AuthKind::Basic) {
        const String encoded = base64Encode(user + ':' + secret);
        return encoded.isEmpty() ? String() : String("Basic ") + encoded;
    }

    if (challenge.kind != AuthKind::Digest) {
        return String();
    }

    String algorithm = challenge.algorithm;
    algorithm.toLowerCase();

    if (algorithm != "md5" && algorithm != "md5-sess" && !algorithm.isEmpty()) {
        return String();
    }

    char cnonceBuffer[33];
    snprintf(
        cnonceBuffer,
        sizeof(cnonceBuffer),
        "%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random())
    );

    char nonceCountBuffer[9];
    snprintf(
        nonceCountBuffer,
        sizeof(nonceCountBuffer),
        "%08lx",
        static_cast<unsigned long>(nonceCount)
    );

    const String cnonce = cnonceBuffer;
    const String nc = nonceCountBuffer;
    String ha1 = md5Hex(user + ':' + challenge.realm + ':' + secret);

    if (algorithm == "md5-sess") {
        ha1 = md5Hex(ha1 + ':' + challenge.nonce + ':' + cnonce);
    }

    const String ha2 = md5Hex(String(method) + ':' + requestUri);
    String selectedQop;

    if (!challenge.qop.isEmpty()) {
        if (!qopSupportsAuth(challenge.qop)) {
            return String();
        }

        selectedQop = "auth";
    }

    const String response = selectedQop.isEmpty()
        ? md5Hex(ha1 + ':' + challenge.nonce + ':' + ha2)
        : md5Hex(
            ha1 + ':' + challenge.nonce + ':' + nc + ':' + cnonce + ':' +
            selectedQop + ':' + ha2
        );

    String authorization = "Digest username=\"";
    authorization += quotedEscape(user);
    authorization += "\", realm=\"";
    authorization += quotedEscape(challenge.realm);
    authorization += "\", nonce=\"";
    authorization += quotedEscape(challenge.nonce);
    authorization += "\", uri=\"";
    authorization += quotedEscape(requestUri);
    authorization += "\", response=\"";
    authorization += response;
    authorization += '"';

    if (!challenge.algorithm.isEmpty()) {
        authorization += ", algorithm=";
        authorization += challenge.algorithm;
    }

    if (!selectedQop.isEmpty()) {
        authorization += ", qop=auth, nc=";
        authorization += nc;
        authorization += ", cnonce=\"";
        authorization += cnonce;
        authorization += '"';
    }

    if (!challenge.opaque.isEmpty()) {
        authorization += ", opaque=\"";
        authorization += quotedEscape(challenge.opaque);
        authorization += '"';
    }

    return authorization;
}

bool ensureCameraConnected(
    RtspSession& session,
    const RtspDeadline* deadline = nullptr
)
{
    if (session.cameraClient.connected()) {
        return true;
    }

    session.cameraClient.stop();
    const uint32_t connectTimeoutMs = boundedTimeoutMs(
        LIVE_RTSP_CONNECT_TIMEOUT_MS,
        deadline
    );

    if (connectTimeoutMs == 0) {
        return false;
    }

    if (!session.cameraClient.connect(
        session.endpoint.ip,
        session.endpoint.port,
        connectTimeoutMs
    )) {
        return false;
    }

    const uint32_t responseTimeoutMs = boundedTimeoutMs(
        LIVE_RTSP_RESPONSE_TIMEOUT_MS,
        deadline
    );

    if (responseTimeoutMs == 0) {
        session.cameraClient.stop();
        return false;
    }

    session.cameraClient.setNoDelay(true);
    session.cameraClient.setTimeout((responseTimeoutMs + 999UL) / 1000UL);
    return true;
}

bool parseBrowserRequest(
    WiFiClient& client,
    String& method,
    String& target
)
{
    String request;
    request.reserve(768);
    unsigned long lastDataAt = millis();

    while (request.length() < MAX_BROWSER_REQUEST_BYTES) {
        while (client.available()) {
            const int value = client.read();

            if (value < 0) {
                break;
            }

            request += static_cast<char>(value);
            lastDataAt = millis();

            if (request.endsWith("\r\n\r\n")) {
                const int lineEnd = request.indexOf("\r\n");
                const String firstLine = lineEnd >= 0
                    ? request.substring(0, lineEnd)
                    : request;
                const int firstSpace = firstLine.indexOf(' ');
                const int secondSpace = firstLine.indexOf(' ', firstSpace + 1);

                if (firstSpace <= 0 || secondSpace <= firstSpace) {
                    return false;
                }

                method = firstLine.substring(0, firstSpace);
                target = firstLine.substring(firstSpace + 1, secondSpace);
                return true;
            }
        }

        if (!client.connected() || millis() - lastDataAt >= BROWSER_REQUEST_TIMEOUT_MS) {
            return false;
        }

        delay(1);
    }

    return false;
}

void sendHttpJson(WiFiClient& client, int statusCode, const String& json)
{
    const char* reason = statusCode == 400
        ? "Bad Request"
        : statusCode == 404
            ? "Not Found"
            : statusCode == 409
                ? "Conflict"
                : statusCode == 503
                    ? "Service Unavailable"
                    : "Error";
    String response = "HTTP/1.1 ";
    response += statusCode;
    response += ' ';
    response += reason;
    response += "\r\nContent-Type: application/json; charset=utf-8\r\n";
    response += "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n";
    response += "Connection: close\r\nContent-Length: ";
    response += json.length();
    response += "\r\n\r\n";
    response += json;
    writeAll(client, response);
}

bool sendStreamHeaders(WiFiClient& client)
{
    return writeAll(
        client,
        String(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/vnd.camera-watchdog.rtp\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Expose-Headers: X-Camera-Watchdog-Stream\r\n"
            "X-Camera-Watchdog-Stream: rtp-h264-v1\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Content-Encoding: identity\r\n"
            "Connection: close\r\n\r\n"
        )
    );
}

bool sendRecord(
    WiFiClient& browser,
    uint8_t type,
    const uint8_t* payload,
    uint32_t length
)
{
    uint8_t header[5] = {
        type,
        static_cast<uint8_t>((length >> 24) & 0xFF),
        static_cast<uint8_t>((length >> 16) & 0xFF),
        static_cast<uint8_t>((length >> 8) & 0xFF),
        static_cast<uint8_t>(length & 0xFF)
    };

    return
        writeAll(browser, header, sizeof(header)) &&
        (length == 0 || writeAll(browser, payload, length));
}

bool sendRecord(WiFiClient& browser, uint8_t type, const String& text)
{
    return sendRecord(
        browser,
        type,
        reinterpret_cast<const uint8_t*>(text.c_str()),
        text.length()
    );
}

String jsonEscapeLocal(const String& value)
{
    String output;
    output.reserve(value.length() + 12);

    for (size_t index = 0; index < value.length(); index++) {
        const char character = value.charAt(index);

        switch (character) {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<uint8_t>(character) >= 0x20) {
                    output += character;
                }
                break;
        }
    }

    return output;
}

size_t parseProfileIndex(const String& target, bool& valid)
{
    valid = false;
    const int question = target.indexOf('?');
    const String path = question >= 0 ? target.substring(0, question) : target;

    if (path != "/live" && path != "/stream") {
        return 0;
    }

    if (question < 0) {
        valid = true;
        return 0;
    }

    const String query = target.substring(question + 1);
    int start = 0;

    while (start <= static_cast<int>(query.length())) {
        const int ampersand = query.indexOf('&', start);
        const int end = ampersand >= 0 ? ampersand : static_cast<int>(query.length());
        const String parameter = query.substring(start, end);
        const int equal = parameter.indexOf('=');

        if (equal > 0 && parameter.substring(0, equal) == "profile") {
            const String value = parameter.substring(equal + 1);

            if (value.isEmpty()) {
                return 0;
            }

            for (size_t index = 0; index < value.length(); index++) {
                if (!isDigit(value.charAt(index))) {
                    return 0;
                }
            }

            const long parsed = value.toInt();

            if (parsed < 0 || parsed > 31) {
                return 0;
            }

            valid = true;
            return static_cast<size_t>(parsed);
        }

        if (ampersand < 0) {
            break;
        }

        start = ampersand + 1;
    }

    valid = true;
    return 0;
}

String makeRtspAuthorization(
    RtspSession& session,
    const char* method,
    const String& requestUri,
    const char* username,
    const char* password
)
{
    if (!session.challenge.valid) {
        return String();
    }

    session.nonceCount++;
    return makeAuthorization(
        method,
        requestUri,
        session.challenge,
        username,
        password,
        session.nonceCount
    );
}

bool sendRtspRequestAttempt(
    RtspSession& session,
    const char* method,
    const String& requestUri,
    const String& extraHeaders,
    const String& body,
    const char* username,
    const char* password,
    const char* userAgent,
    bool withAuthorization,
    RtspResponse& response,
    const RtspDeadline* deadline = nullptr
)
{
    if (!ensureCameraConnected(session, deadline)) {
        return false;
    }

    String request;
    request.reserve(
        requestUri.length() + extraHeaders.length() + body.length() + 768
    );
    request += method;
    request += ' ';
    request += requestUri;
    request += " RTSP/1.0\r\nCSeq: ";
    request += session.cseq++;
    request += "\r\nUser-Agent: ";
    request += userAgent == nullptr ? "ESP32-Camera-Watchdog" : userAgent;
    request += "\r\n";

    if (withAuthorization && session.challenge.valid) {
        const String authorization = makeRtspAuthorization(
            session,
            method,
            requestUri,
            username,
            password
        );

        if (authorization.isEmpty()) {
            return false;
        }

        request += "Authorization: ";
        request += authorization;
        request += "\r\n";
    }

    if (!session.sessionId.isEmpty()) {
        request += "Session: ";
        request += session.sessionId;
        request += "\r\n";
    }

    request += extraHeaders;

    if (!body.isEmpty()) {
        request += "Content-Length: ";
        request += body.length();
        request += "\r\n";
    }

    request += "\r\n";
    request += body;

    if (!writeAll(session.cameraClient, request, deadline)) {
        return false;
    }

    return readRtspResponse(
        session.cameraClient,
        response,
        '\0',
        deadline
    );
}

bool performRtspRequest(
    RtspSession& session,
    const char* method,
    const String& requestUri,
    const String& extraHeaders,
    const String& body,
    const char* username,
    const char* password,
    const char* userAgent,
    RtspResponse& response,
    String& error,
    const RtspDeadline* deadline = nullptr,
    bool* protocolResponded = nullptr,
    int* lastStatusCode = nullptr
)
{
    const bool haveChallenge = session.challenge.valid;
    const auto captureStatus = [&]() {
        if (response.statusCode <= 0) {
            return;
        }

        if (protocolResponded != nullptr) {
            *protocolResponded = true;
        }

        if (lastStatusCode != nullptr) {
            *lastStatusCode = response.statusCode;
        }
    };

    const bool firstAttemptSucceeded = sendRtspRequestAttempt(
        session,
        method,
        requestUri,
        extraHeaders,
        body,
        username,
        password,
        userAgent,
        haveChallenge,
        response,
        deadline
    );
    captureStatus();

    if (!firstAttemptSucceeded) {
        error = "A câmera encerrou a conexão RTSP durante ";
        error += method;
        return false;
    }

    if (response.statusCode != 401) {
        return true;
    }

    const String authenticate = headerValue(response.headers, "WWW-Authenticate");

    if (
        username == nullptr ||
        username[0] == '\0' ||
        !parseAuthChallenge(authenticate, session.challenge)
    ) {
        error = "A câmera exige autenticação RTSP e não aceitou as credenciais";
        return false;
    }

    if (!session.cameraClient.connected()) {
        session.cameraClient.stop();
    }

    const bool authenticatedAttemptSucceeded = sendRtspRequestAttempt(
        session,
        method,
        requestUri,
        extraHeaders,
        body,
        username,
        password,
        userAgent,
        true,
        response,
        deadline
    );
    captureStatus();

    if (!authenticatedAttemptSucceeded) {
        error = "Falha ao repetir ";
        error += method;
        error += " com autenticação RTSP";
        return false;
    }

    if (response.statusCode == 401) {
        error = "Usuário ou senha RTSP recusados pela câmera";
        return false;
    }

    return true;
}

String parseSessionId(const String& sessionHeader)
{
    const int semicolon = sessionHeader.indexOf(';');
    String value = semicolon >= 0
        ? sessionHeader.substring(0, semicolon)
        : sessionHeader;
    value.trim();
    return value;
}

uint8_t parseInterleavedChannel(const String& transportHeader)
{
    String normalized = transportHeader;
    normalized.toLowerCase();
    const int marker = normalized.indexOf("interleaved=");

    if (marker < 0) {
        return 0;
    }

    const int start = marker + 12;
    int end = start;

    while (
        end < static_cast<int>(transportHeader.length()) &&
        isDigit(transportHeader.charAt(end))
    ) {
        end++;
    }

    if (end == start) {
        return 0;
    }

    return static_cast<uint8_t>(transportHeader.substring(start, end).toInt());
}

String extractFmtpParameter(const String& fmtp, const char* name)
{
    String normalized = fmtp;
    normalized.toLowerCase();
    String marker = String(name);
    marker.toLowerCase();
    marker += '=';
    const int position = normalized.indexOf(marker);

    if (position < 0) {
        return String();
    }

    const int start = position + marker.length();
    const int semicolon = fmtp.indexOf(';', start);
    String value = semicolon >= 0
        ? fmtp.substring(start, semicolon)
        : fmtp.substring(start);
    value.trim();
    return value;
}

struct SdpPayloadDescription {
    uint8_t payloadType = 0;
    uint32_t clockRate = 90000;
    String codec;
    String fmtp;
};

bool parseSdpVideoTrack(
    const String& sdp,
    SdpVideoTrack& selected,
    String& error
)
{
    selected = SdpVideoTrack{};
    String mediaLine;
    String control;
    String seenVideoCodec;
    SdpPayloadDescription payloads[16];
    size_t payloadCount = 0;
    bool inVideoSection = false;
    int start = 0;

    while (start <= static_cast<int>(sdp.length())) {
        int end = sdp.indexOf('\n', start);

        if (end < 0) {
            end = sdp.length();
        }

        String line = sdp.substring(start, end);
        line.trim();

        if (line.startsWith("m=")) {
            if (inVideoSection) {
                break;
            }

            inVideoSection = line.startsWith("m=video");

            if (inVideoSection) {
                mediaLine = line;
            }
        } else if (inVideoSection) {
            if (line.startsWith("a=control:")) {
                control = trimmed(line.substring(10));
            } else if (line.startsWith("a=rtpmap:")) {
                const int space = line.indexOf(' ');

                if (space > 9) {
                    const long parsedPayload = line.substring(9, space).toInt();

                    if (parsedPayload >= 0 && parsedPayload <= 127) {
                        const String mapping = line.substring(space + 1);
                        const int slash = mapping.indexOf('/');
                        String codec = slash >= 0
                            ? mapping.substring(0, slash)
                            : mapping;
                        codec.trim();
                        uint32_t clockRate = 90000;

                        if (slash >= 0) {
                            const int nextSlash = mapping.indexOf('/', slash + 1);
                            const String clockText = nextSlash >= 0
                                ? mapping.substring(slash + 1, nextSlash)
                                : mapping.substring(slash + 1);
                            const long parsedClock = clockText.toInt();

                            if (parsedClock > 0) {
                                clockRate = static_cast<uint32_t>(parsedClock);
                            }
                        }

                        size_t payloadIndex = payloadCount;

                        for (size_t index = 0; index < payloadCount; index++) {
                            if (payloads[index].payloadType == parsedPayload) {
                                payloadIndex = index;
                                break;
                            }
                        }

                        if (payloadIndex == payloadCount && payloadCount < 16) {
                            payloads[payloadCount].payloadType =
                                static_cast<uint8_t>(parsedPayload);
                            payloadCount++;
                        }

                        if (payloadIndex < payloadCount) {
                            payloads[payloadIndex].codec = codec;
                            payloads[payloadIndex].clockRate = clockRate;
                        }

                        if (seenVideoCodec.isEmpty() && !codec.isEmpty()) {
                            seenVideoCodec = codec;
                        }
                    }
                }
            } else if (line.startsWith("a=fmtp:")) {
                const int space = line.indexOf(' ');

                if (space > 7) {
                    const long parsedPayload = line.substring(7, space).toInt();

                    if (parsedPayload >= 0 && parsedPayload <= 127) {
                        size_t payloadIndex = payloadCount;

                        for (size_t index = 0; index < payloadCount; index++) {
                            if (payloads[index].payloadType == parsedPayload) {
                                payloadIndex = index;
                                break;
                            }
                        }

                        if (payloadIndex == payloadCount && payloadCount < 16) {
                            payloads[payloadCount].payloadType =
                                static_cast<uint8_t>(parsedPayload);
                            payloadCount++;
                        }

                        if (payloadIndex < payloadCount) {
                            payloads[payloadIndex].fmtp = line.substring(space + 1);
                        }
                    }
                }
            }
        }

        if (end >= static_cast<int>(sdp.length())) {
            break;
        }

        start = end + 1;
    }

    if (mediaLine.isEmpty()) {
        error = "O SDP RTSP não contém uma faixa de vídeo";
        return false;
    }

    for (size_t index = 0; index < payloadCount; index++) {
        if (lowerCopy(payloads[index].codec) != "h264") {
            continue;
        }

        if (control.isEmpty()) {
            error = "A faixa H.264 do SDP não informou a URI de controle";
            return false;
        }

        selected.valid = true;
        selected.codec = "H264";
        selected.payloadType = payloads[index].payloadType;
        selected.clockRate = payloads[index].clockRate;
        selected.control = control;
        selected.spropParameterSets = extractFmtpParameter(
            payloads[index].fmtp,
            "sprop-parameter-sets"
        );
        return true;
    }

    error = seenVideoCodec.isEmpty()
        ? "O SDP RTSP não contém um payload de vídeo reconhecido"
        : String("O perfil selecionado usa ") + seenVideoCodec +
            "; o player integrado requer H.264";
    return false;
}

String rtspOrigin(const ParsedRtspUri& endpoint)
{
    return String("rtsp://") + endpoint.authority;
}

String normalizeRtspRequestUri(
    const ParsedRtspUri& endpoint,
    const String& candidate
)
{
    String uri = candidate;
    uri.trim();

    if (uri.isEmpty()) {
        return endpoint.requestUri;
    }

    String normalized = uri;
    normalized.toLowerCase();

    if (!normalized.startsWith("rtsp://")) {
        const String origin = rtspOrigin(endpoint);

        if (uri.startsWith("/")) {
            return origin + uri;
        }

        String basePath = endpoint.path;
        const int baseQuery = basePath.indexOf('?');

        if (baseQuery >= 0) {
            basePath = basePath.substring(0, baseQuery);
        }

        if (uri.startsWith("?")) {
            return origin + basePath + uri;
        }

        const int lastSlash = basePath.lastIndexOf('/');
        basePath = lastSlash >= 0
            ? basePath.substring(0, lastSlash + 1)
            : String("/");
        return origin + basePath + uri;
    }

    const int authorityStart = 7;
    const int slash = uri.indexOf('/', authorityStart);
    const int query = uri.indexOf('?', authorityStart);
    int suffixStart = -1;

    if (slash >= 0 && query >= 0) {
        suffixStart = min(slash, query);
    } else if (slash >= 0) {
        suffixStart = slash;
    } else if (query >= 0) {
        suffixStart = query;
    }

    String result = rtspOrigin(endpoint);

    if (suffixStart < 0) {
        result += '/';
        return result;
    }

    if (uri.charAt(suffixStart) == '?') {
        result += '/';
    }

    result += uri.substring(suffixStart);
    return result;
}

String resolveTrackUri(
    const ParsedRtspUri& endpoint,
    const String& aggregateUri,
    const String& contentBase,
    const String& control
)
{
    if (control.startsWith("rtsp://")) {
        return normalizeRtspRequestUri(endpoint, control);
    }

    if (control == "*") {
        return aggregateUri;
    }

    if (control.startsWith("/")) {
        return rtspOrigin(endpoint) + control;
    }

    String base = contentBase;
    base.trim();

    if (base.isEmpty()) {
        base = aggregateUri;
    }

    base = normalizeRtspRequestUri(endpoint, base);

    if (!base.endsWith("/")) {
        base += '/';
    }

    return base + control;
}

String makePlayingStatusJson(
    const LiveStreamDescriptor& descriptor,
    const SdpVideoTrack& track
)
{
    String json;
    json.reserve(640 + track.spropParameterSets.length());
    json += "{\"state\":\"playing\",\"detail\":\"RTSP conectado pelo ESP32\",\"profile\":\"";
    json += jsonEscapeLocal(descriptor.profileName);
    json += "\",\"codec\":\"";
    json += jsonEscapeLocal(track.codec);
    json += "\",\"encoding\":\"";
    json += jsonEscapeLocal(descriptor.encoding);
    json += "\",\"width\":";
    json += descriptor.width;
    json += ",\"height\":";
    json += descriptor.height;
    json += ",\"declaredFps\":";
    json += descriptor.declaredFps;
    json += ",\"declaredBitrateKbps\":";
    json += descriptor.declaredBitrateKbps;
    json += ",\"payloadType\":";
    json += track.payloadType;
    json += ",\"clockRate\":";
    json += track.clockRate;
    json += ",\"spropParameterSets\":\"";
    json += jsonEscapeLocal(track.spropParameterSets);
    json += "\"}";
    return json;
}

bool sendKeepalive(
    RtspSession& session,
    const char* username,
    const char* password,
    const char* userAgent
)
{
    if (!session.cameraClient.connected()) {
        return false;
    }

    String request;
    request.reserve(700);
    request += "OPTIONS ";
    const String requestUri = session.aggregateUri.isEmpty()
        ? session.endpoint.requestUri
        : session.aggregateUri;
    request += requestUri;
    request += " RTSP/1.0\r\nCSeq: ";
    request += session.cseq++;
    request += "\r\nUser-Agent: ";
    request += userAgent == nullptr ? "ESP32-Camera-Watchdog" : userAgent;
    request += "\r\n";

    if (!session.sessionId.isEmpty()) {
        request += "Session: ";
        request += session.sessionId;
        request += "\r\n";
    }

    if (session.challenge.valid) {
        const String authorization = makeRtspAuthorization(
            session,
            "OPTIONS",
            requestUri,
            username,
            password
        );

        if (!authorization.isEmpty()) {
            request += "Authorization: ";
            request += authorization;
            request += "\r\n";
        }
    }

    request += "\r\n";
    return writeAll(session.cameraClient, request);
}

void sendTeardown(
    RtspSession& session,
    const char* username,
    const char* password,
    const char* userAgent,
    const RtspDeadline* deadline = nullptr
)
{
    if (!session.cameraClient.connected() || session.sessionId.isEmpty()) {
        return;
    }

    String request;
    request.reserve(700);
    request += "TEARDOWN ";
    const String requestUri = session.aggregateUri.isEmpty()
        ? session.endpoint.requestUri
        : session.aggregateUri;
    request += requestUri;
    request += " RTSP/1.0\r\nCSeq: ";
    request += session.cseq++;
    request += "\r\nUser-Agent: ";
    request += userAgent == nullptr ? "ESP32-Camera-Watchdog" : userAgent;
    request += "\r\nSession: ";
    request += session.sessionId;
    request += "\r\n";

    if (session.challenge.valid) {
        const String authorization = makeRtspAuthorization(
            session,
            "TEARDOWN",
            requestUri,
            username,
            password
        );

        if (!authorization.isEmpty()) {
            request += "Authorization: ";
            request += authorization;
            request += "\r\n";
        }
    }

    request += "\r\n";
    writeAll(session.cameraClient, request, deadline);
}

RelaySessionResult relayRtspToBrowser(
    WiFiClient& browser,
    const LiveStreamDescriptor& descriptor,
    const char* username,
    const char* password,
    const char* userAgent,
    LiveStreamSynchronizationRequester synchronizationRequester,
    uint32_t& lastSyncAttemptAtMs,
    bool& hasSyncAttempt,
    bool& lastSyncAccepted,
    LiveRelayStats& relayStats,
    portMUX_TYPE& statsMux
)
{
    RelaySessionResult result;
    const unsigned long sessionStartedAt = millis();
    RtspSession session;

    const auto finishSession = [&]() -> RelaySessionResult {
        sendTeardown(session, username, password, userAgent);
        session.cameraClient.stop();
        result.durationSeconds = (millis() - sessionStartedAt) / 1000UL;
        return result;
    };

    if (!parseRtspUri(descriptor.uri, session.endpoint)) {
        result.error = "A câmera retornou uma URI RTSP inválida";
        return finishSession();
    }

    if (!sendRecord(
        browser,
        LIVE_RECORD_STATUS,
        String("{\"state\":\"connecting\",\"detail\":\"Abrindo RTSP na câmera\"}")
    )) {
        result.normalBrowserClose = true;
        return finishSession();
    }

    RtspResponse describe;
    String requestError;

    if (!performRtspRequest(
        session,
        "DESCRIBE",
        session.endpoint.requestUri,
        "Accept: application/sdp\r\n",
        String(),
        username,
        password,
        userAgent,
        describe,
        requestError
    )) {
        result.error = requestError;
        return finishSession();
    }

    if (describe.statusCode < 200 || describe.statusCode >= 300) {
        result.error = "DESCRIBE RTSP retornou código ";
        result.error += describe.statusCode;
        return finishSession();
    }

    session.contentBase = headerValue(describe.headers, "Content-Base");
    session.aggregateUri = normalizeRtspRequestUri(
        session.endpoint,
        session.contentBase.isEmpty()
            ? session.endpoint.requestUri
            : session.contentBase
    );
    String sdpError;

    if (!parseSdpVideoTrack(describe.body, session.track, sdpError)) {
        result.error = sdpError;
        return finishSession();
    }

    session.trackUri = resolveTrackUri(
        session.endpoint,
        session.aggregateUri,
        session.contentBase,
        session.track.control
    );

    describe = RtspResponse{};

    std::unique_ptr<uint8_t[]> packet(
        new (std::nothrow) uint8_t[LIVE_RTP_PACKET_MAX_BYTES]
    );

    if (!packet) {
        result.error = "Heap insuficiente para o buffer RTP";
        return finishSession();
    }

    RtspResponse setup;

    if (!performRtspRequest(
        session,
        "SETUP",
        session.trackUri,
        "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n",
        String(),
        username,
        password,
        userAgent,
        setup,
        requestError
    )) {
        result.error = requestError;
        return finishSession();
    }

    if (setup.statusCode < 200 || setup.statusCode >= 300) {
        result.error = "SETUP RTSP retornou código ";
        result.error += setup.statusCode;
        return finishSession();
    }

    session.sessionId = parseSessionId(headerValue(setup.headers, "Session"));

    if (session.sessionId.isEmpty()) {
        result.error = "A câmera não retornou o identificador da sessão RTSP";
        return finishSession();
    }

    session.videoChannel = parseInterleavedChannel(
        headerValue(setup.headers, "Transport")
    );
    setup = RtspResponse{};

    RtspResponse play;

    if (!performRtspRequest(
        session,
        "PLAY",
        session.aggregateUri,
        "Range: npt=0.000-\r\n",
        String(),
        username,
        password,
        userAgent,
        play,
        requestError
    )) {
        result.error = requestError;
        return finishSession();
    }

    if (play.statusCode < 200 || play.statusCode >= 300) {
        result.error = "PLAY RTSP retornou código ";
        result.error += play.statusCode;
        return finishSession();
    }

    play = RtspResponse{};

    if (!sendRecord(
        browser,
        LIVE_RECORD_STATUS,
        makePlayingStatusJson(descriptor, session.track)
    )) {
        result.normalBrowserClose = true;
        return finishSession();
    }

    result.started = true;
    unsigned long lastMediaAt = millis();
    unsigned long lastKeepaliveAt = millis();
    bool synchronizationRequested = false;

    while (browser.connected() && session.cameraClient.connected()) {
        const unsigned long now = millis();

        if (now - lastKeepaliveAt >= LIVE_RTSP_KEEPALIVE_INTERVAL_MS) {
            if (!sendKeepalive(
                session,
                username,
                password,
                userAgent
            )) {
                result.error = "Falha ao manter a sessão RTSP ativa";
                break;
            }

            lastKeepaliveAt = millis();
        }

        if (session.cameraClient.available() <= 0) {
            if (now - lastMediaAt >= LIVE_RTSP_PACKET_TIMEOUT_MS) {
                result.error = "A câmera ficou sem entregar pacotes RTP";
                break;
            }

            delay(1);
            continue;
        }

        const int first = session.cameraClient.read();

        if (first < 0) {
            delay(1);
            continue;
        }

        if (first == '$') {
            uint8_t interleavedHeader[3];

            if (!readExact(
                session.cameraClient,
                interleavedHeader,
                sizeof(interleavedHeader),
                LIVE_RTSP_RESPONSE_TIMEOUT_MS
            )) {
                result.error = "Cabeçalho RTP intercalado incompleto";
                break;
            }

            const uint8_t channel = interleavedHeader[0];
            const size_t length =
                (static_cast<size_t>(interleavedHeader[1]) << 8) |
                static_cast<size_t>(interleavedHeader[2]);

            if (length > LIVE_RTP_PACKET_MAX_BYTES) {
                result.oversizedPackets++;

                if (!discardExact(
                    session.cameraClient,
                    length,
                    LIVE_RTSP_RESPONSE_TIMEOUT_MS
                )) {
                    result.error = "Pacote RTP grande e incompleto";
                    break;
                }

                continue;
            }

            if (!readExact(
                session.cameraClient,
                packet.get(),
                length,
                LIVE_RTSP_RESPONSE_TIMEOUT_MS
            )) {
                result.error = "Pacote RTP incompleto";
                break;
            }

            if (channel != session.videoChannel) {
                continue;
            }

            lastMediaAt = millis();

            if (!sendRecord(
                browser,
                LIVE_RECORD_RTP,
                packet.get(),
                static_cast<uint32_t>(length)
            )) {
                result.normalBrowserClose = true;
                break;
            }

            result.packets++;
            result.bytes += length;
            const uint32_t relayedAtMs = millis();

            portENTER_CRITICAL(&statsMux);
            relayStats.mediaActive = true;
            relayStats.lastMediaPacketAtMs = relayedAtMs;
            portEXIT_CRITICAL(&statsMux);

            if (!synchronizationRequested) {
                synchronizationRequested = true;
                requestSynchronizationPoint(
                    descriptor,
                    synchronizationRequester,
                    lastSyncAttemptAtMs,
                    hasSyncAttempt,
                    lastSyncAccepted,
                    relayStats,
                    statsMux
                );
            }

            continue;
        }

        RtspResponse asynchronousResponse;

        if (!readRtspResponse(
            session.cameraClient,
            asynchronousResponse,
            static_cast<char>(first)
        )) {
            result.error = "Resposta RTSP assíncrona inválida";
            break;
        }
    }

    if (!browser.connected() && result.error.isEmpty()) {
        result.normalBrowserClose = true;
    } else if (!session.cameraClient.connected() && result.error.isEmpty()) {
        result.error = "A câmera encerrou a conexão RTSP";
    }

    return finishSession();
}

}

LiveStreamRelay::LiveStreamRelay() = default;

bool LiveStreamRelay::begin(
    uint16_t port,
    LiveStreamDescriptorProvider descriptorProvider,
    LiveStreamSynchronizationRequester synchronizationRequester,
    SemaphoreHandle_t cameraIoMutex,
    const char* username,
    const char* password,
    const char* userAgent
)
{
    if (
        port == 0 ||
        descriptorProvider == nullptr ||
        cameraIoMutex == nullptr ||
        taskHandle_ != nullptr
    ) {
        return false;
    }

    port_ = port;
    descriptorProvider_ = descriptorProvider;
    synchronizationRequester_ = synchronizationRequester;
    cameraIoMutex_ = cameraIoMutex;
    username_ = username;
    password_ = password;
    userAgent_ = userAgent;
    server_ = new (std::nothrow) WiFiServer(port_);

    if (server_ == nullptr) {
        return false;
    }

    server_->begin();
    server_->setNoDelay(true);

    portENTER_CRITICAL(&statsMux_);
    stats_.running = true;
    stats_.port = port_;
    stats_.lastError[0] = '\0';
    portEXIT_CRITICAL(&statsMux_);

    const BaseType_t created = xTaskCreatePinnedToCore(
        taskEntry,
        "camera-live",
        LIVE_TASK_STACK_BYTES,
        this,
        LIVE_TASK_PRIORITY,
        &taskHandle_,
        LIVE_TASK_CORE
    );

    if (created != pdPASS) {
        server_->stop();
        delete server_;
        server_ = nullptr;
        taskHandle_ = nullptr;

        portENTER_CRITICAL(&statsMux_);
        stats_.running = false;
        portEXIT_CRITICAL(&statsMux_);
        return false;
    }

    return true;
}

void LiveStreamRelay::snapshotStats(LiveRelayStats& output) const
{
    portENTER_CRITICAL(&statsMux_);
    output = stats_;
    portEXIT_CRITICAL(&statsMux_);
}

void LiveStreamRelay::invalidateMediaEvidence()
{
    portENTER_CRITICAL(&statsMux_);
    stats_.mediaActive = false;
    stats_.lastMediaPacketAtMs = 0;
    portEXIT_CRITICAL(&statsMux_);
}

bool LiveStreamRelay::probeMedia(
    size_t profileIndex,
    uint32_t timeoutMs,
    LiveMediaProbeResult& result
)
{
    result = LiveMediaProbeResult{};
    result.attempted = true;
    const uint32_t startedAtMs = millis();
    const uint32_t proportionalReserveMs = timeoutMs / 10UL;
    const uint32_t teardownReserveMs = proportionalReserveMs <
        LIVE_PROBE_TEARDOWN_RESERVE_MS
            ? proportionalReserveMs
            : LIVE_PROBE_TEARDOWN_RESERVE_MS;
    RtspDeadline totalDeadline;
    totalDeadline.startedAtMs = startedAtMs;
    totalDeadline.timeoutMs = timeoutMs;
    RtspDeadline workDeadline;
    workDeadline.startedAtMs = startedAtMs;
    workDeadline.timeoutMs = timeoutMs - teardownReserveMs;
    RtspSession session;
    uint32_t playAcceptedAtMs = 0;

    const auto finishProbe = [&](const String& detail) {
        if (result.playAccepted && playAcceptedAtMs != 0) {
            result.observedAfterPlayMs = millis() - playAcceptedAtMs;
        }

        sendTeardown(
            session,
            username_,
            password_,
            userAgent_,
            &totalDeadline
        );
        session.cameraClient.stop();
        result.latencyMs = millis() - startedAtMs;
        copyText(result.detail, sizeof(result.detail), detail);
        return result.mediaReceived;
    };

    if (timeoutMs == 0) {
        return finishProbe("Sonda RTP sem tempo disponivel");
    }

    if (descriptorProvider_ == nullptr) {
        return finishProbe("Provedor de perfil ao vivo indisponivel");
    }

    LiveStreamDescriptor descriptor;
    String detail;

    const uint32_t descriptorTimeoutMs = remainingTimeMs(&workDeadline);

    if (
        descriptorTimeoutMs == 0 ||
        !descriptorProvider_(
            profileIndex,
            descriptor,
            detail,
            descriptorTimeoutMs
        )
    ) {
        if (detail.isEmpty()) {
            detail = "Perfil de video indisponivel para a sonda RTP";
        }

        return finishProbe(detail);
    }

    result.descriptorResolved = true;

    if (remainingTimeMs(&workDeadline) == 0) {
        return finishProbe("Timeout ao resolver o perfil da sonda RTP");
    }

    if (!parseRtspUri(descriptor.uri, session.endpoint)) {
        return finishProbe("URI RTSP invalida para a sonda RTP");
    }

    RtspResponse describe;

    if (!performRtspRequest(
        session,
        "DESCRIBE",
        session.endpoint.requestUri,
        "Accept: application/sdp\r\n",
        String(),
        username_,
        password_,
        userAgent_,
        describe,
        detail,
        &workDeadline,
        &result.rtspResponded,
        &result.statusCode
    )) {
        return finishProbe(detail.isEmpty()
            ? String("Falha no DESCRIBE da sonda RTP")
            : detail);
    }

    if (describe.statusCode < 200 || describe.statusCode >= 300) {
        detail = "DESCRIBE RTSP retornou codigo ";
        detail += describe.statusCode;
        return finishProbe(detail);
    }

    session.contentBase = headerValue(describe.headers, "Content-Base");
    session.aggregateUri = normalizeRtspRequestUri(
        session.endpoint,
        session.contentBase.isEmpty()
            ? session.endpoint.requestUri
            : session.contentBase
    );

    if (!parseSdpVideoTrack(describe.body, session.track, detail)) {
        return finishProbe(detail);
    }

    session.trackUri = resolveTrackUri(
        session.endpoint,
        session.aggregateUri,
        session.contentBase,
        session.track.control
    );
    describe = RtspResponse{};

    RtspResponse setup;

    if (!performRtspRequest(
        session,
        "SETUP",
        session.trackUri,
        "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n",
        String(),
        username_,
        password_,
        userAgent_,
        setup,
        detail,
        &workDeadline,
        &result.rtspResponded,
        &result.statusCode
    )) {
        return finishProbe(detail.isEmpty()
            ? String("Falha no SETUP da sonda RTP")
            : detail);
    }

    if (setup.statusCode < 200 || setup.statusCode >= 300) {
        detail = "SETUP RTSP retornou codigo ";
        detail += setup.statusCode;
        return finishProbe(detail);
    }

    session.sessionId = parseSessionId(headerValue(setup.headers, "Session"));

    if (session.sessionId.isEmpty()) {
        return finishProbe("SETUP nao retornou identificador de sessao");
    }

    session.videoChannel = parseInterleavedChannel(
        headerValue(setup.headers, "Transport")
    );
    setup = RtspResponse{};

    RtspResponse play;

    if (!performRtspRequest(
        session,
        "PLAY",
        session.aggregateUri,
        "Range: npt=0.000-\r\n",
        String(),
        username_,
        password_,
        userAgent_,
        play,
        detail,
        &workDeadline,
        &result.rtspResponded,
        &result.statusCode
    )) {
        return finishProbe(detail.isEmpty()
            ? String("Falha no PLAY da sonda RTP")
            : detail);
    }

    if (play.statusCode < 200 || play.statusCode >= 300) {
        detail = "PLAY RTSP retornou codigo ";
        detail += play.statusCode;
        return finishProbe(detail);
    }

    result.playAccepted = true;
    playAcceptedAtMs = millis();
    play = RtspResponse{};

    while (remainingTimeMs(&workDeadline) > 0) {
        if (!session.cameraClient.connected()) {
            return finishProbe("Camera encerrou a sonda antes do primeiro RTP");
        }

        if (session.cameraClient.available() <= 0) {
            delay(1);
            continue;
        }

        const int first = session.cameraClient.read();

        if (first < 0) {
            delay(1);
            continue;
        }

        if (first != '$') {
            RtspResponse asynchronousResponse;

            if (!readRtspResponse(
                session.cameraClient,
                asynchronousResponse,
                static_cast<char>(first),
                &workDeadline
            )) {
                return finishProbe("Resposta RTSP assincrona invalida na sonda");
            }

            continue;
        }

        uint8_t interleavedHeader[3];

        if (!readExact(
            session.cameraClient,
            interleavedHeader,
            sizeof(interleavedHeader),
            LIVE_RTSP_RESPONSE_TIMEOUT_MS,
            &workDeadline
        )) {
            return finishProbe("Cabecalho RTP incompleto na sonda");
        }

        const uint8_t channel = interleavedHeader[0];
        const size_t length =
            (static_cast<size_t>(interleavedHeader[1]) << 8) |
            static_cast<size_t>(interleavedHeader[2]);

        if (length < 12) {
            if (!discardExact(
                session.cameraClient,
                length,
                LIVE_RTSP_RESPONSE_TIMEOUT_MS,
                &workDeadline
            )) {
                return finishProbe("Pacote intercalado curto e incompleto");
            }

            continue;
        }

        uint8_t rtpHeader[12];

        if (!readExact(
            session.cameraClient,
            rtpHeader,
            sizeof(rtpHeader),
            LIVE_RTSP_RESPONSE_TIMEOUT_MS,
            &workDeadline
        )) {
            return finishProbe("Cabecalho do pacote RTP incompleto");
        }

        if (!discardExact(
            session.cameraClient,
            length - sizeof(rtpHeader),
            LIVE_RTSP_RESPONSE_TIMEOUT_MS,
            &workDeadline
        )) {
            return finishProbe("Payload do pacote RTP incompleto");
        }

        const size_t rtpHeaderLength =
            12U + 4U * static_cast<size_t>(rtpHeader[0] & 0x0F);
        const bool validRtp =
            channel == session.videoChannel &&
            (rtpHeader[0] >> 6) == 2 &&
            length >= rtpHeaderLength &&
            (rtpHeader[1] & 0x7F) == session.track.payloadType;

        if (!validRtp) {
            continue;
        }

        result.mediaReceived = true;
        result.bytes = static_cast<uint32_t>(length);
        return finishProbe("Primeiro pacote RTP de video recebido");
    }

    return finishProbe("Timeout aguardando o primeiro pacote RTP de video");
}

void LiveStreamRelay::taskEntry(void* parameter)
{
    LiveStreamRelay* relay = static_cast<LiveStreamRelay*>(parameter);

    if (relay != nullptr) {
        relay->run();
    }

    vTaskDelete(nullptr);
}

void LiveStreamRelay::run()
{
    while (true) {
        if (WiFi.status() != WL_CONNECTED || server_ == nullptr) {
            delay(50);
            continue;
        }

        WiFiClient browserClient = server_->available();

        if (!browserClient) {
            delay(10);
            continue;
        }

        handleBrowserClient(browserClient);
        browserClient.stop();
        delay(2);
    }
}

void LiveStreamRelay::handleBrowserClient(WiFiClient browserClient)
{
    if (!browserClient || !browserClient.connected()) {
        return;
    }

    browserClient.setNoDelay(true);
    browserClient.setTimeout((BROWSER_REQUEST_TIMEOUT_MS + 999UL) / 1000UL);

    String method;
    String target;

    if (!parseBrowserRequest(browserClient, method, target)) {
        sendHttpJson(
            browserClient,
            400,
            "{\"ok\":false,\"error\":\"invalid_request\"}"
        );
        return;
    }

    if (method == "OPTIONS") {
        writeAll(
            browserClient,
            String(
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Accept\r\n"
                "Connection: close\r\n\r\n"
            )
        );
        return;
    }

    if (method != "GET") {
        sendHttpJson(
            browserClient,
            400,
            "{\"ok\":false,\"error\":\"method_not_allowed\"}"
        );
        return;
    }

    bool targetValid = false;
    const size_t profileIndex = parseProfileIndex(target, targetValid);

    if (!targetValid) {
        sendHttpJson(
            browserClient,
            404,
            "{\"ok\":false,\"error\":\"stream_not_found\"}"
        );
        return;
    }

    LiveStreamDescriptor descriptor;
    String descriptorError;

    if (xSemaphoreTakeRecursive(cameraIoMutex_, 0) != pdTRUE) {
        sendHttpJson(
            browserClient,
            503,
            "{\"ok\":false,\"error\":\"camera_busy\",\"detail\":\"Verificacao da camera em andamento\"}"
        );
        return;
    }

    if (!descriptorProvider_(
        profileIndex,
        descriptor,
        descriptorError,
        LIVE_RTSP_RESPONSE_TIMEOUT_MS
    )) {
        xSemaphoreGiveRecursive(cameraIoMutex_);
        String json = "{\"ok\":false,\"error\":\"profile_unavailable\",\"detail\":\"";
        json += jsonEscapeLocal(descriptorError);
        json += "\"}";
        sendHttpJson(browserClient, 503, json);
        return;
    }

    if (!sendStreamHeaders(browserClient)) {
        xSemaphoreGiveRecursive(cameraIoMutex_);
        return;
    }

    portENTER_CRITICAL(&statsMux_);
    stats_.clientActive = true;
    stats_.mediaActive = false;
    stats_.sessions++;
    portEXIT_CRITICAL(&statsMux_);

    Serial.printf(
        "LIVE: navegador conectado; perfil=%u; %s; %ux%u; %u FPS\n",
        static_cast<unsigned>(profileIndex),
        descriptor.encoding,
        static_cast<unsigned>(descriptor.width),
        static_cast<unsigned>(descriptor.height),
        static_cast<unsigned>(descriptor.declaredFps)
    );

    const RelaySessionResult result = relayRtspToBrowser(
        browserClient,
        descriptor,
        username_,
        password_,
        userAgent_,
        synchronizationRequester_,
        lastSyncAttemptAtMs_,
        hasSyncAttempt_,
        lastSyncAccepted_,
        stats_,
        statsMux_
    );

    if (!result.error.isEmpty() && browserClient.connected()) {
        String json = "{\"state\":\"error\",\"detail\":\"";
        json += jsonEscapeLocal(result.error);
        json += "\"}";
        sendRecord(browserClient, LIVE_RECORD_ERROR, json);
    }

    if (!result.error.isEmpty()) {
        Serial.printf("LIVE: sessão encerrada com erro: %s\n", result.error.c_str());
    } else {
        Serial.printf(
            "LIVE: sessão encerrada pelo navegador; pacotes=%lu; bytes=%llu; duração=%lus\n",
            static_cast<unsigned long>(result.packets),
            static_cast<unsigned long long>(result.bytes),
            static_cast<unsigned long>(result.durationSeconds)
        );
    }

    portENTER_CRITICAL(&statsMux_);
    stats_.clientActive = false;
    stats_.mediaActive = false;
    stats_.packetsRelayed += result.packets;
    stats_.bytesRelayed += result.bytes;
    stats_.oversizedPackets += result.oversizedPackets;
    stats_.lastSessionDurationSeconds = result.durationSeconds;

    if (!result.error.isEmpty()) {
        stats_.failedSessions++;
        copyText(stats_.lastError, sizeof(stats_.lastError), result.error);
    } else if (result.started || result.normalBrowserClose) {
        stats_.lastError[0] = '\0';
    }

    portEXIT_CRITICAL(&statsMux_);
    xSemaphoreGiveRecursive(cameraIoMutex_);
}
