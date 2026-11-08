#include "firmware_internal.h"
#include "live_stream.h"

#include <errno.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <mbedtls/md5.h>

namespace CameraWatchdog {
namespace {

constexpr size_t RX_CAPACITY = 4096;
constexpr size_t TX_CAPACITY = 1800;
constexpr size_t REQUEST_CAPACITY = 1024;
constexpr size_t HEADER_CAPACITY = 1024;
constexpr size_t BODY_CAPACITY = 1536;
constexpr size_t URL_CAPACITY = 512;
constexpr size_t PARAM_SET_CAPACITY = 128;

constexpr uint32_t RTSP_CONNECT_TIMEOUT_MS = 2500;
constexpr uint32_t RTSP_TRANSACTION_TIMEOUT_MS = 3500;
constexpr uint32_t RTSP_STALL_TIMEOUT_MS = 6000;
constexpr uint32_t RTSP_DEFAULT_KEEPALIVE_MS = 20000;
constexpr uint32_t SESSION_IDLE_GRACE_MS = 4000;
constexpr uint32_t CLIENT_WRITE_WAIT_MS = 200;
constexpr uint32_t CLIENT_WRITE_TIMEOUT_MS = 1500;
constexpr uint32_t HANDSHAKE_WAIT_MS = 400;
constexpr uint32_t RETRY_BASE_MS = 1500;
constexpr uint32_t RETRY_MAX_MS = 8000;
constexpr uint8_t FAILURES_BEFORE_NOTIFYING_CLIENTS = 4;
constexpr size_t MINIMUM_FREE_HEAP_BYTES = 36000;
constexpr uint32_t TASK_STACK_BYTES = 10240;

constexpr uint8_t FLAG_MARKER = 0x01;
constexpr uint8_t FLAG_DISCONTINUITY = 0x02;

struct Client {
    int fd = -1;
    bool needKey = true;
    bool discontinuity = true;
};

enum class SendResult : uint8_t {
    Sent,
    Dropped,
    Closed
};

Client clients[LIVE_STREAM_MAX_CLIENTS];
int listenFd = -1;
TaskHandle_t taskHandle = nullptr;
portMUX_TYPE targetMux = portMUX_INITIALIZER_UNLOCKED;
LiveStreamTarget publishedTarget;
uint32_t publishedGeneration = 0;

struct Session {
    bool active = false;
    int fd = -1;
    uint32_t generation = 0;
    uint32_t cseq = 0;
    uint8_t videoPayloadType = 96;
    uint8_t videoChannel = 0;
    uint32_t keepaliveIntervalMs = RTSP_DEFAULT_KEEPALIVE_MS;
    unsigned long startedAtMs = 0;
    unsigned long lastKeepaliveAtMs = 0;
    unsigned long lastPacketAtMs = 0;
    bool haveSequence = false;
    uint16_t lastSequence = 0;
    char sessionId[48] = {0};
    char authScheme = 0;
    char authRealm[64] = {0};
    char authNonce[96] = {0};
    bool authQopAuth = false;
    uint32_t nonceCount = 0;
    char url[URL_CAPACITY] = {0};
    char controlUrl[URL_CAPACITY] = {0};
    LiveStreamTarget target;
};

Session session;
uint8_t rxBuffer[RX_CAPACITY];
size_t rxLength = 0;
uint8_t txBuffer[TX_CAPACITY];
uint8_t paramBuffer[TX_CAPACITY];
char requestBuffer[REQUEST_CAPACITY];
char headerBuffer[HEADER_CAPACITY];
char bodyBuffer[BODY_CAPACITY];
uint8_t spsCache[PARAM_SET_CAPACITY];
uint8_t ppsCache[PARAM_SET_CAPACITY];
size_t spsCacheLength = 0;
size_t ppsCacheLength = 0;

unsigned long idleSinceMs = 0;
unsigned long retryAtMs = 0;
uint8_t consecutiveFailures = 0;

volatile LiveStreamState stateValue = LiveStreamState::Idle;
const char* volatile lastErrorText = "";
volatile uint32_t statSessionsStarted = 0;
volatile uint32_t statSessionFailures = 0;
volatile uint32_t statFrames = 0;
volatile uint32_t statPackets = 0;
volatile uint32_t statDropped = 0;
volatile uint32_t statClientsServed = 0;
volatile uint32_t statClientsRejected = 0;
volatile uint32_t statBytesFromCameraLow = 0;
volatile uint32_t statBytesFromCameraHigh = 0;
volatile uint32_t statBytesToClientsLow = 0;
volatile uint32_t statBytesToClientsHigh = 0;
volatile unsigned long statLastFrameAtMs = 0;
volatile uint32_t statLastFrameCameraId = 0;
volatile uint16_t statFps = 0;
volatile uint32_t statKbps = 0;
volatile uint8_t statClientCount = 0;
volatile unsigned long statSessionStartedAtMs = 0;

uint64_t fromCameraTotal = 0;
uint64_t toClientsTotal = 0;
uint32_t rateFrames = 0;
uint64_t rateBytes = 0;
unsigned long rateWindowStartedAtMs = 0;

void addFromCamera(size_t bytes)
{
    fromCameraTotal += bytes;
    statBytesFromCameraLow = static_cast<uint32_t>(fromCameraTotal);
    statBytesFromCameraHigh = static_cast<uint32_t>(fromCameraTotal >> 32);
}

void addToClients(size_t bytes)
{
    toClientsTotal += bytes;
    statBytesToClientsLow = static_cast<uint32_t>(toClientsTotal);
    statBytesToClientsHigh = static_cast<uint32_t>(toClientsTotal >> 32);
}

bool equalsIgnoreCasePrefix(const char* text, const char* prefix)
{
    while (*prefix != '\0') {
        if (*text == '\0' || tolower(static_cast<unsigned char>(*text)) != tolower(static_cast<unsigned char>(*prefix)))
            return false;
        text++;
        prefix++;
    }
    return true;
}

const char* findIgnoreCase(const char* haystack, const char* needle)
{
    const size_t needleLength = strlen(needle);
    if (needleLength == 0)
        return haystack;
    for (; *haystack != '\0'; haystack++) {
        if (equalsIgnoreCasePrefix(haystack, needle))
            return haystack;
    }
    return nullptr;
}

bool headerValue(const char* headers, const char* name, char* out, size_t capacity, size_t occurrence = 0)
{
    const size_t nameLength = strlen(name);
    const char* line = headers;
    size_t seen = 0;
    while (*line != '\0') {
        const char* end = strstr(line, "\r\n");
        const size_t lineLength = end != nullptr ? static_cast<size_t>(end - line) : strlen(line);
        if (lineLength > nameLength && equalsIgnoreCasePrefix(line, name) && line[nameLength] == ':') {
            if (seen++ == occurrence) {
                const char* value = line + nameLength + 1;
                const char* valueEnd = line + lineLength;
                while (value < valueEnd && (*value == ' ' || *value == '\t'))
                    value++;
                while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
                    valueEnd--;
                const size_t length = min(static_cast<size_t>(valueEnd - value), capacity - 1);
                memcpy(out, value, length);
                out[length] = '\0';
                return true;
            }
        }
        if (end == nullptr)
            break;
        line = end + 2;
    }
    out[0] = '\0';
    return false;
}

bool parameterValue(const char* text, const char* key, char* out, size_t capacity)
{
    const size_t keyLength = strlen(key);
    for (const char* cursor = text; *cursor != '\0'; cursor++) {
        const bool boundary = cursor == text || cursor[-1] == ' ' || cursor[-1] == ',' || cursor[-1] == ';';
        if (!boundary || !equalsIgnoreCasePrefix(cursor, key) || cursor[keyLength] != '=')
            continue;
        const char* value = cursor + keyLength + 1;
        const bool quoted = *value == '"';
        if (quoted)
            value++;
        size_t length = 0;
        while (value[length] != '\0' && (quoted ? value[length] != '"' : (value[length] != ',' && value[length] != ';' && value[length] != ' ')))
            length++;
        length = min(length, capacity - 1);
        memcpy(out, value, length);
        out[length] = '\0';
        return true;
    }
    out[0] = '\0';
    return false;
}

void setLastError(const char* text)
{
    lastErrorText = text;
}

void closeSocket(int& fd)
{
    if (fd >= 0) {
        lwip_close(fd);
        fd = -1;
    }
}

void configureStreamSocket(int fd)
{
    const int enabled = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    const int flags = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool waitSocket(int fd, bool forWrite, uint32_t timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000UL;
    timeout.tv_usec = (timeoutMs % 1000UL) * 1000UL;
    const int ready = lwip_select(fd + 1, forWrite ? nullptr : &set, forWrite ? &set : nullptr, nullptr, &timeout);
    return ready > 0;
}

bool sendAll(int fd, const uint8_t* data, size_t length, uint32_t timeoutMs)
{
    const unsigned long startedAtMs = millis();
    size_t offset = 0;
    while (offset < length) {
        const int written = lwip_send(fd, data + offset, length - offset, 0);
        if (written > 0) {
            offset += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            return false;
        const unsigned long elapsed = millis() - startedAtMs;
        if (elapsed >= timeoutMs)
            return false;
        waitSocket(fd, true, min<uint32_t>(timeoutMs - elapsed, 50));
    }
    return true;
}

int connectTcp(const uint8_t ip[4], uint16_t port, uint32_t timeoutMs)
{
    const int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    configureStreamSocket(fd);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    memcpy(&address.sin_addr.s_addr, ip, 4);
    const int result = lwip_connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result < 0 && errno != EINPROGRESS) {
        lwip_close(fd);
        return -1;
    }
    if (result < 0) {
        if (!waitSocket(fd, true, timeoutMs)) {
            lwip_close(fd);
            return -1;
        }
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) < 0 || socketError != 0) {
            lwip_close(fd);
            return -1;
        }
    }
    return fd;
}

size_t writeFrameHeader(uint8_t* out, uint8_t opcode, size_t payloadLength)
{
    out[0] = static_cast<uint8_t>(0x80 | opcode);
    if (payloadLength < 126) {
        out[1] = static_cast<uint8_t>(payloadLength);
        return 2;
    }
    out[1] = 126;
    out[2] = static_cast<uint8_t>(payloadLength >> 8);
    out[3] = static_cast<uint8_t>(payloadLength);
    return 4;
}

bool sendTextFrame(int fd, uint8_t opcode, const char* text)
{
    uint8_t frame[160];
    const size_t length = min(strlen(text), sizeof(frame) - 4);
    const size_t headerLength = writeFrameHeader(frame, opcode, length);
    memcpy(frame + headerLength, text, length);
    return sendAll(fd, frame, headerLength + length, 500);
}

void sendCloseFrame(int fd, const char* reason)
{
    uint8_t frame[128];
    const size_t reasonLength = min(strlen(reason), sizeof(frame) - 8);
    const size_t headerLength = writeFrameHeader(frame, 0x8, reasonLength + 2);
    frame[headerLength] = 0x03;
    frame[headerLength + 1] = 0xF3;
    memcpy(frame + headerLength + 2, reason, reasonLength);
    sendAll(fd, frame, headerLength + 2 + reasonLength, 300);
}

size_t clientCount()
{
    size_t count = 0;
    for (const Client& client : clients) {
        if (client.fd >= 0)
            count++;
    }
    return count;
}

void closeClient(Client& client)
{
    closeSocket(client.fd);
    client.needKey = true;
    client.discontinuity = true;
    statClientCount = static_cast<uint8_t>(clientCount());
}

void flagClientsForResync()
{
    for (Client& client : clients) {
        client.needKey = true;
        client.discontinuity = true;
    }
}

void closeAllClients(const char* reason)
{
    for (Client& client : clients) {
        if (client.fd < 0)
            continue;
        sendCloseFrame(client.fd, reason);
        closeClient(client);
    }
}

size_t buildBinaryFrame(uint8_t* out, size_t capacity, uint8_t flags, uint32_t timestamp, const uint8_t* payload, size_t payloadLength, size_t& flagsIndex)
{
    const size_t messageLength = 5 + payloadLength;
    if (messageLength + 4 > capacity)
        return 0;
    const size_t headerLength = writeFrameHeader(out, 0x2, messageLength);
    out[headerLength] = flags;
    out[headerLength + 1] = static_cast<uint8_t>(timestamp >> 24);
    out[headerLength + 2] = static_cast<uint8_t>(timestamp >> 16);
    out[headerLength + 3] = static_cast<uint8_t>(timestamp >> 8);
    out[headerLength + 4] = static_cast<uint8_t>(timestamp);
    memcpy(out + headerLength + 5, payload, payloadLength);
    flagsIndex = headerLength;
    return headerLength + messageLength;
}

SendResult sendToClient(Client& client, const uint8_t* frame, size_t length)
{
    if (!waitSocket(client.fd, true, CLIENT_WRITE_WAIT_MS))
        return SendResult::Dropped;
    if (!sendAll(client.fd, frame, length, CLIENT_WRITE_TIMEOUT_MS))
        return SendResult::Closed;
    addToClients(length);
    return SendResult::Sent;
}

void dispatchPacket(uint8_t firstNal, bool marker, uint32_t timestamp, const uint8_t* payload, size_t payloadLength)
{
    size_t flagsIndex = 0;
    const uint8_t baseFlags = marker ? FLAG_MARKER : 0;
    const size_t frameLength = buildBinaryFrame(txBuffer, sizeof(txBuffer), baseFlags, timestamp, payload, payloadLength, flagsIndex);
    if (frameLength == 0)
        return;

    for (Client& client : clients) {
        if (client.fd < 0)
            continue;

        if (client.needKey) {
            if (firstNal == 7) {
                client.needKey = false;
            } else if (firstNal == 5 && spsCacheLength != 0 && ppsCacheLength != 0) {
                size_t paramFlagsIndex = 0;
                const size_t spsLength = buildBinaryFrame(paramBuffer, sizeof(paramBuffer), FLAG_DISCONTINUITY, timestamp, spsCache, spsCacheLength, paramFlagsIndex);
                if (spsLength == 0 || sendToClient(client, paramBuffer, spsLength) != SendResult::Sent)
                    continue;
                const size_t ppsLength = buildBinaryFrame(paramBuffer, sizeof(paramBuffer), 0, timestamp, ppsCache, ppsCacheLength, paramFlagsIndex);
                if (ppsLength == 0 || sendToClient(client, paramBuffer, ppsLength) != SendResult::Sent) {
                    client.needKey = true;
                    continue;
                }
                client.needKey = false;
                client.discontinuity = false;
            } else {
                continue;
            }
        }

        txBuffer[flagsIndex] = baseFlags | (client.discontinuity ? FLAG_DISCONTINUITY : 0);
        const SendResult result = sendToClient(client, txBuffer, frameLength);
        if (result == SendResult::Sent) {
            client.discontinuity = false;
        } else if (result == SendResult::Dropped) {
            client.needKey = true;
            client.discontinuity = true;
            statDropped = statDropped + 1;
        } else {
            closeClient(client);
        }
    }
}

void rejectConnection(int fd, const char* statusLine)
{
    char response[160];
    const int length = snprintf(response, sizeof(response), "HTTP/1.1 %s\r\nConnection: close\r\nContent-Length: 0\r\nCache-Control: no-store\r\n\r\n", statusLine);
    sendAll(fd, reinterpret_cast<const uint8_t*>(response), static_cast<size_t>(length), 300);
    lwip_close(fd);
    statClientsRejected = statClientsRejected + 1;
}

bool computeWebSocketAccept(const char* key, char* out, size_t capacity)
{
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char material[128];
    const size_t keyLength = strlen(key);
    if (keyLength == 0 || keyLength > 60 || capacity < 32)
        return false;
    memcpy(material, key, keyLength);
    memcpy(material + keyLength, GUID, sizeof(GUID) - 1);
    unsigned char digest[20];
    if (mbedtls_sha1_ret(reinterpret_cast<const unsigned char*>(material), keyLength + sizeof(GUID) - 1, digest) != 0)
        return false;
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out), capacity, &written, digest, sizeof(digest)) != 0)
        return false;
    out[written] = '\0';
    return true;
}

void acceptClient()
{
    sockaddr_in remote = {};
    socklen_t remoteLength = sizeof(remote);
    const int fd = lwip_accept(listenFd, reinterpret_cast<sockaddr*>(&remote), &remoteLength);
    if (fd < 0)
        return;
    configureStreamSocket(fd);

    char request[768];
    size_t received = 0;
    const unsigned long startedAtMs = millis();
    bool complete = false;
    while (millis() - startedAtMs < HANDSHAKE_WAIT_MS && received < sizeof(request) - 1) {
        if (!waitSocket(fd, false, 50))
            continue;
        const int count = lwip_recv(fd, request + received, sizeof(request) - 1 - received, 0);
        if (count <= 0) {
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            lwip_close(fd);
            return;
        }
        received += static_cast<size_t>(count);
        request[received] = '\0';
        if (strstr(request, "\r\n\r\n") != nullptr) {
            complete = true;
            break;
        }
    }
    if (!complete) {
        rejectConnection(fd, "408 Request Timeout");
        return;
    }

    char key[80];
    if (strncmp(request, "GET /live", 9) != 0 || findIgnoreCase(request, "upgrade: websocket") == nullptr || !headerValue(strstr(request, "\r\n") + 2, "Sec-WebSocket-Key", key, sizeof(key))) {
        rejectConnection(fd, "400 Bad Request");
        return;
    }

    LiveStreamTarget target;
    taskENTER_CRITICAL(&targetMux);
    target = publishedTarget;
    taskEXIT_CRITICAL(&targetMux);

    const char* cameraParameter = strstr(request, "camera=");
    const uint32_t requestedCamera = cameraParameter != nullptr ? static_cast<uint32_t>(strtoul(cameraParameter + 7, nullptr, 10)) : 0;
    if (!target.valid || (requestedCamera != 0 && requestedCamera != target.cameraId)) {
        rejectConnection(fd, "409 Conflict");
        return;
    }

    Client* slot = nullptr;
    for (Client& client : clients) {
        if (client.fd < 0) {
            slot = &client;
            break;
        }
    }
    if (slot == nullptr) {
        rejectConnection(fd, "503 Service Unavailable");
        return;
    }

    char accept[40];
    if (!computeWebSocketAccept(key, accept, sizeof(accept))) {
        rejectConnection(fd, "400 Bad Request");
        return;
    }

    char response[200];
    const int responseLength = snprintf(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nCache-Control: no-store\r\n\r\n", accept);
    if (!sendAll(fd, reinterpret_cast<const uint8_t*>(response), static_cast<size_t>(responseLength), 500)) {
        lwip_close(fd);
        return;
    }

    char hello[150];
    snprintf(hello, sizeof(hello), "{\"type\":\"hello\",\"profile\":\"%s\",\"width\":%u,\"height\":%u,\"hq\":%s}", target.profileName, static_cast<unsigned>(target.width), static_cast<unsigned>(target.height), target.highQuality ? "true" : "false");
    if (!sendTextFrame(fd, 0x1, hello)) {
        lwip_close(fd);
        return;
    }

    slot->fd = fd;
    slot->needKey = true;
    slot->discontinuity = true;
    statClientsServed = statClientsServed + 1;
    statClientCount = static_cast<uint8_t>(clientCount());
    Serial.printf("[live] viewer connected (%u active)\n", static_cast<unsigned>(clientCount()));
}

void readClient(Client& client)
{
    uint8_t scratch[64];
    const int count = lwip_recv(client.fd, scratch, sizeof(scratch), 0);
    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        closeClient(client);
        return;
    }
    if (count > 0 && (scratch[0] & 0x0F) == 0x8)
        closeClient(client);
}

void md5Hex(const char* input, char out[33])
{
    unsigned char digest[16];
    mbedtls_md5_ret(reinterpret_cast<const unsigned char*>(input), strlen(input), digest);
    for (size_t index = 0; index < 16; index++)
        snprintf(out + index * 2, 3, "%02x", digest[index]);
    out[32] = '\0';
}

void authorizationLine(const char* method, const char* uri, char* out, size_t capacity)
{
    out[0] = '\0';
    const char* username = session.target.username;
    const char* password = session.target.password;
    if (session.authScheme == 'b') {
        char credentials[200];
        const int credentialsLength = snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
        unsigned char encoded[300];
        size_t written = 0;
        if (credentialsLength > 0 && mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &written, reinterpret_cast<const unsigned char*>(credentials), static_cast<size_t>(credentialsLength)) == 0) {
            encoded[written] = '\0';
            snprintf(out, capacity, "Authorization: Basic %s\r\n", encoded);
        }
        return;
    }
    if (session.authScheme != 'd')
        return;
    char material[300];
    char ha1[33];
    char ha2[33];
    char response[33];
    snprintf(material, sizeof(material), "%s:%s:%s", username, session.authRealm, password);
    md5Hex(material, ha1);
    snprintf(material, sizeof(material), "%s:%s", method, uri);
    md5Hex(material, ha2);
    if (session.authQopAuth) {
        session.nonceCount++;
        char cnonce[9];
        snprintf(cnonce, sizeof(cnonce), "%08lx", static_cast<unsigned long>(esp_random()));
        snprintf(material, sizeof(material), "%s:%s:%08lx:%s:auth:%s", ha1, session.authNonce, static_cast<unsigned long>(session.nonceCount), cnonce, ha2);
        md5Hex(material, response);
        snprintf(out, capacity, "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", qop=auth, nc=%08lx, cnonce=\"%s\"\r\n", username, session.authRealm, session.authNonce, uri, response, static_cast<unsigned long>(session.nonceCount), cnonce);
    } else {
        snprintf(material, sizeof(material), "%s:%s:%s", ha1, session.authNonce, ha2);
        md5Hex(material, response);
        snprintf(out, capacity, "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"\r\n", username, session.authRealm, session.authNonce, uri, response);
    }
}

void consumeRx(size_t bytes)
{
    if (bytes >= rxLength) {
        rxLength = 0;
        return;
    }
    memmove(rxBuffer, rxBuffer + bytes, rxLength - bytes);
    rxLength -= bytes;
}

struct Reply {
    int status = 0;
    size_t bodyLength = 0;
};

bool rtspTransact(const char* method, const char* uri, const char* extraHeaders, Reply& reply)
{
    reply = Reply{};
    char authorization[420];
    authorizationLine(method, uri, authorization, sizeof(authorization));
    session.cseq++;
    const int requestLength = snprintf(requestBuffer, sizeof(requestBuffer), "%s %s RTSP/1.0\r\nCSeq: %lu\r\nUser-Agent: CameraWatchdog/%s\r\n%s%s\r\n", method, uri, static_cast<unsigned long>(session.cseq), FIRMWARE_VERSION, authorization, extraHeaders);
    if (requestLength <= 0 || static_cast<size_t>(requestLength) >= sizeof(requestBuffer))
        return false;
    if (!sendAll(session.fd, reinterpret_cast<const uint8_t*>(requestBuffer), static_cast<size_t>(requestLength), RTSP_TRANSACTION_TIMEOUT_MS))
        return false;

    const unsigned long startedAtMs = millis();
    size_t headerEnd = 0;
    while (true) {
        for (size_t index = 0; index + 3 < rxLength; index++) {
            if (rxBuffer[index] == '\r' && rxBuffer[index + 1] == '\n' && rxBuffer[index + 2] == '\r' && rxBuffer[index + 3] == '\n') {
                headerEnd = index + 4;
                break;
            }
        }
        if (headerEnd != 0)
            break;
        if (rxLength >= sizeof(rxBuffer) || millis() - startedAtMs >= RTSP_TRANSACTION_TIMEOUT_MS)
            return false;
        if (!waitSocket(session.fd, false, 100))
            continue;
        const int count = lwip_recv(session.fd, rxBuffer + rxLength, sizeof(rxBuffer) - rxLength, 0);
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            return false;
        if (count > 0) {
            rxLength += static_cast<size_t>(count);
            addFromCamera(static_cast<size_t>(count));
        }
    }

    const size_t headerCopy = min(headerEnd, sizeof(headerBuffer) - 1);
    memcpy(headerBuffer, rxBuffer, headerCopy);
    headerBuffer[headerCopy] = '\0';
    if (strncmp(headerBuffer, "RTSP/1.0 ", 9) != 0)
        return false;
    reply.status = atoi(headerBuffer + 9);

    char lengthText[16];
    size_t contentLength = 0;
    if (headerValue(headerBuffer, "Content-Length", lengthText, sizeof(lengthText)))
        contentLength = static_cast<size_t>(strtoul(lengthText, nullptr, 10));
    if (headerEnd + contentLength > sizeof(rxBuffer))
        return false;
    while (rxLength < headerEnd + contentLength) {
        if (millis() - startedAtMs >= RTSP_TRANSACTION_TIMEOUT_MS)
            return false;
        if (!waitSocket(session.fd, false, 100))
            continue;
        const int count = lwip_recv(session.fd, rxBuffer + rxLength, sizeof(rxBuffer) - rxLength, 0);
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            return false;
        if (count > 0) {
            rxLength += static_cast<size_t>(count);
            addFromCamera(static_cast<size_t>(count));
        }
    }
    reply.bodyLength = min(contentLength, sizeof(bodyBuffer) - 1);
    memcpy(bodyBuffer, rxBuffer + headerEnd, reply.bodyLength);
    bodyBuffer[reply.bodyLength] = '\0';
    consumeRx(headerEnd + contentLength);
    return true;
}

void captureAuthChallenge()
{
    char challenge[300];
    for (size_t occurrence = 0; occurrence < 4; occurrence++) {
        if (!headerValue(headerBuffer, "WWW-Authenticate", challenge, sizeof(challenge), occurrence))
            break;
        if (equalsIgnoreCasePrefix(challenge, "Digest ")) {
            parameterValue(challenge + 7, "realm", session.authRealm, sizeof(session.authRealm));
            parameterValue(challenge + 7, "nonce", session.authNonce, sizeof(session.authNonce));
            char qop[32];
            session.authQopAuth = parameterValue(challenge + 7, "qop", qop, sizeof(qop)) && strstr(qop, "auth") != nullptr;
            session.nonceCount = 0;
            session.authScheme = 'd';
            break;
        }
        if (equalsIgnoreCasePrefix(challenge, "Basic"))
            session.authScheme = 'b';
    }
}

bool parseSdp(const char* sdp, const char* contentBase, const char* requestUrl)
{
    const char* video = strstr(sdp, "m=video");
    if (video == nullptr)
        return false;
    const char* sectionEnd = strstr(video + 7, "\nm=");
    char section[BODY_CAPACITY];
    const size_t sectionLength = min(sectionEnd != nullptr ? static_cast<size_t>(sectionEnd - video) : strlen(video), sizeof(section) - 1);
    memcpy(section, video, sectionLength);
    section[sectionLength] = '\0';

    const char* payloadTypeText = strstr(section, "RTP/AVP");
    if (payloadTypeText != nullptr)
        session.videoPayloadType = static_cast<uint8_t>(atoi(payloadTypeText + 7));
    const char* rtpmap = findIgnoreCase(section, "a=rtpmap:");
    if (rtpmap == nullptr || findIgnoreCase(rtpmap, "H264/") == nullptr || findIgnoreCase(rtpmap, "H264/") - rtpmap > 40) {
        setLastError("camera_codec_not_h264");
        return false;
    }
    session.videoPayloadType = static_cast<uint8_t>(atoi(rtpmap + 9));

    char control[300] = {0};
    const char* controlLine = findIgnoreCase(section, "a=control:");
    if (controlLine != nullptr) {
        controlLine += 10;
        size_t length = 0;
        while (controlLine[length] != '\0' && controlLine[length] != '\r' && controlLine[length] != '\n' && length < sizeof(control) - 1)
            length++;
        memcpy(control, controlLine, length);
        control[length] = '\0';
    }

    if (equalsIgnoreCasePrefix(control, "rtsp://")) {
        snprintf(session.controlUrl, sizeof(session.controlUrl), "%s", control);
    } else {
        const char* base = contentBase[0] != '\0' ? contentBase : requestUrl;
        const size_t baseLength = strlen(base);
        const bool needsSlash = baseLength > 0 && base[baseLength - 1] != '/' && control[0] != '\0' && control[0] != '/';
        const int written = snprintf(session.controlUrl, sizeof(session.controlUrl), "%s%s%s", base, needsSlash ? "/" : "", control[0] == '*' ? "" : control);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(session.controlUrl))
            return false;
    }
    return true;
}

void sendTeardown()
{
    if (session.fd < 0 || session.sessionId[0] == '\0')
        return;
    char authorization[420];
    authorizationLine("TEARDOWN", session.url, authorization, sizeof(authorization));
    session.cseq++;
    const int length = snprintf(requestBuffer, sizeof(requestBuffer), "TEARDOWN %s RTSP/1.0\r\nCSeq: %lu\r\nSession: %s\r\n%s\r\n", session.url, static_cast<unsigned long>(session.cseq), session.sessionId, authorization);
    if (length > 0 && static_cast<size_t>(length) < sizeof(requestBuffer))
        sendAll(session.fd, reinterpret_cast<const uint8_t*>(requestBuffer), static_cast<size_t>(length), 200);
}

void stopSession(LiveStreamState nextState, const char* error)
{
    if (session.fd >= 0) {
        if (session.active)
            sendTeardown();
        closeSocket(session.fd);
    }
    const bool wasActive = session.active;
    session.active = false;
    session.sessionId[0] = '\0';
    session.authScheme = 0;
    rxLength = 0;
    spsCacheLength = 0;
    ppsCacheLength = 0;
    stateValue = nextState;
    if (error != nullptr)
        setLastError(error);
    flagClientsForResync();
    statFps = 0;
    statKbps = 0;
    if (wasActive)
        Serial.printf("[live] RTSP session closed (%s)\n", error != nullptr ? error : "idle");
}

bool startSession()
{
    if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_BYTES) {
        setLastError("low_memory");
        return false;
    }

    LiveStreamTarget target;
    uint32_t generation = 0;
    taskENTER_CRITICAL(&targetMux);
    target = publishedTarget;
    generation = publishedGeneration;
    taskEXIT_CRITICAL(&targetMux);
    if (!target.valid) {
        setLastError("no_target");
        return false;
    }

    session = Session{};
    session.target = target;
    session.generation = generation;
    rxLength = 0;
    spsCacheLength = 0;
    ppsCacheLength = 0;
    stateValue = LiveStreamState::Connecting;
    statSessionsStarted = statSessionsStarted + 1;

    session.fd = connectTcp(target.ip, target.port, RTSP_CONNECT_TIMEOUT_MS);
    if (session.fd < 0) {
        setLastError("camera_unreachable");
        return false;
    }

    snprintf(session.url, sizeof(session.url), "rtsp://%u.%u.%u.%u:%u%s", target.ip[0], target.ip[1], target.ip[2], target.ip[3], static_cast<unsigned>(target.port), target.path);

    Reply reply;
    if (!rtspTransact("DESCRIBE", session.url, "Accept: application/sdp\r\n", reply)) {
        setLastError("rtsp_describe_failed");
        return false;
    }
    if (reply.status == 401) {
        captureAuthChallenge();
        if (session.authScheme == 0 || target.username[0] == '\0') {
            setLastError("rtsp_auth_required");
            return false;
        }
        if (!rtspTransact("DESCRIBE", session.url, "Accept: application/sdp\r\n", reply)) {
            setLastError("rtsp_describe_failed");
            return false;
        }
    }
    if (reply.status == 401 || reply.status == 403) {
        setLastError("rtsp_auth_rejected");
        return false;
    }
    if (reply.status != 200) {
        setLastError("rtsp_describe_failed");
        return false;
    }

    char contentBase[URL_CAPACITY] = {0};
    if (!headerValue(headerBuffer, "Content-Base", contentBase, sizeof(contentBase)))
        headerValue(headerBuffer, "Content-Location", contentBase, sizeof(contentBase));
    if (!parseSdp(bodyBuffer, contentBase, session.url)) {
        if (lastErrorText[0] == '\0')
            setLastError("rtsp_sdp_invalid");
        return false;
    }
    snprintf(session.url, sizeof(session.url), "%s", contentBase[0] != '\0' ? contentBase : session.url);

    if (!rtspTransact("SETUP", session.controlUrl, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n", reply) || reply.status != 200) {
        setLastError(reply.status == 461 ? "rtsp_transport_unsupported" : "rtsp_setup_failed");
        return false;
    }
    char sessionHeader[96];
    if (!headerValue(headerBuffer, "Session", sessionHeader, sizeof(sessionHeader))) {
        setLastError("rtsp_setup_failed");
        return false;
    }
    char* separator = strchr(sessionHeader, ';');
    if (separator != nullptr) {
        char timeoutText[24];
        if (parameterValue(separator + 1, "timeout", timeoutText, sizeof(timeoutText))) {
            const uint32_t timeoutSeconds = static_cast<uint32_t>(strtoul(timeoutText, nullptr, 10));
            if (timeoutSeconds >= 10)
                session.keepaliveIntervalMs = min<uint32_t>(RTSP_DEFAULT_KEEPALIVE_MS, timeoutSeconds * 500UL);
        }
        *separator = '\0';
    }
    if (strlcpy(session.sessionId, sessionHeader, sizeof(session.sessionId)) >= sizeof(session.sessionId)) {
        setLastError("rtsp_setup_failed");
        return false;
    }

    char transport[120];
    if (headerValue(headerBuffer, "Transport", transport, sizeof(transport))) {
        const char* interleaved = strstr(transport, "interleaved=");
        if (interleaved != nullptr)
            session.videoChannel = static_cast<uint8_t>(atoi(interleaved + 12));
    }

    char sessionLine[80];
    snprintf(sessionLine, sizeof(sessionLine), "Session: %s\r\nRange: npt=0.000-\r\n", session.sessionId);
    if (!rtspTransact("PLAY", session.url, sessionLine, reply) || reply.status != 200) {
        setLastError("rtsp_play_failed");
        return false;
    }

    session.active = true;
    session.startedAtMs = millis();
    session.lastKeepaliveAtMs = session.startedAtMs;
    session.lastPacketAtMs = session.startedAtMs;
    statSessionStartedAtMs = session.startedAtMs;
    stateValue = LiveStreamState::Streaming;
    setLastError("");
    flagClientsForResync();
    rateWindowStartedAtMs = session.startedAtMs;
    rateFrames = 0;
    rateBytes = fromCameraTotal;
    Serial.printf("[live] RTSP session active: camera %lu, profile %s, %ux%u\n", static_cast<unsigned long>(target.cameraId), target.profileName, static_cast<unsigned>(target.width), static_cast<unsigned>(target.height));
    return true;
}

void sendKeepalive()
{
    char authorization[420];
    authorizationLine("GET_PARAMETER", session.url, authorization, sizeof(authorization));
    session.cseq++;
    const int length = snprintf(requestBuffer, sizeof(requestBuffer), "GET_PARAMETER %s RTSP/1.0\r\nCSeq: %lu\r\nSession: %s\r\n%s\r\n", session.url, static_cast<unsigned long>(session.cseq), session.sessionId, authorization);
    if (length > 0 && static_cast<size_t>(length) < sizeof(requestBuffer) && !sendAll(session.fd, reinterpret_cast<const uint8_t*>(requestBuffer), static_cast<size_t>(length), 300))
        session.lastPacketAtMs = 0;
}

void handleRtpPacket(const uint8_t* packet, size_t length)
{
    if (length < 12 || (packet[0] >> 6) != 2)
        return;
    size_t headerLength = 12 + static_cast<size_t>(packet[0] & 0x0F) * 4;
    if (packet[0] & 0x10) {
        if (length < headerLength + 4)
            return;
        headerLength += 4 + static_cast<size_t>((packet[headerLength + 2] << 8) | packet[headerLength + 3]) * 4;
    }
    const size_t padding = (packet[0] & 0x20) ? packet[length - 1] : 0;
    if (length <= headerLength + padding)
        return;
    if ((packet[1] & 0x7F) != session.videoPayloadType)
        return;

    const uint8_t* payload = packet + headerLength;
    const size_t payloadLength = length - headerLength - padding;
    const bool marker = (packet[1] & 0x80) != 0;
    const uint16_t sequence = static_cast<uint16_t>((packet[2] << 8) | packet[3]);
    const uint32_t timestamp = (static_cast<uint32_t>(packet[4]) << 24) | (static_cast<uint32_t>(packet[5]) << 16) | (static_cast<uint32_t>(packet[6]) << 8) | packet[7];

    if (session.haveSequence && sequence != static_cast<uint16_t>(session.lastSequence + 1))
        flagClientsForResync();
    session.haveSequence = true;
    session.lastSequence = sequence;

    const uint8_t nalType = payload[0] & 0x1F;
    uint8_t firstNal = nalType;
    if (nalType == 28) {
        firstNal = payloadLength >= 2 && (payload[1] & 0x80) ? (payload[1] & 0x1F) : 0xFF;
    } else if (nalType == 24) {
        firstNal = payloadLength >= 4 ? (payload[3] & 0x1F) : 0xFF;
    } else if (nalType == 7 && payloadLength <= PARAM_SET_CAPACITY) {
        memcpy(spsCache, payload, payloadLength);
        spsCacheLength = payloadLength;
    } else if (nalType == 8 && payloadLength <= PARAM_SET_CAPACITY) {
        memcpy(ppsCache, payload, payloadLength);
        ppsCacheLength = payloadLength;
    }

    statPackets = statPackets + 1;
    session.lastPacketAtMs = millis();
    if (marker) {
        statFrames = statFrames + 1;
        rateFrames++;
        statLastFrameAtMs = session.lastPacketAtMs;
        statLastFrameCameraId = session.target.cameraId;
    }
    dispatchPacket(firstNal, marker, timestamp, payload, payloadLength);
}

bool pumpRtsp()
{
    if (rxLength < sizeof(rxBuffer)) {
        const int count = lwip_recv(session.fd, rxBuffer + rxLength, sizeof(rxBuffer) - rxLength, 0);
        if (count == 0) {
            setLastError("camera_stream_closed");
            return false;
        }
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                setLastError("camera_stream_closed");
                return false;
            }
        } else {
            rxLength += static_cast<size_t>(count);
            addFromCamera(static_cast<size_t>(count));
        }
    }

    size_t position = 0;
    while (position < rxLength) {
        if (rxBuffer[position] == '$') {
            if (rxLength - position < 4)
                break;
            const size_t packetLength = (static_cast<size_t>(rxBuffer[position + 2]) << 8) | rxBuffer[position + 3];
            if (packetLength + 4 > sizeof(rxBuffer)) {
                setLastError("rtsp_protocol_error");
                return false;
            }
            if (rxLength - position < packetLength + 4)
                break;
            if (rxBuffer[position + 1] == session.videoChannel)
                handleRtpPacket(rxBuffer + position + 4, packetLength);
            position += packetLength + 4;
        } else if (rxLength - position >= 5 && memcmp(rxBuffer + position, "RTSP/", 5) == 0) {
            size_t end = 0;
            for (size_t index = position; index + 3 < rxLength; index++) {
                if (memcmp(rxBuffer + index, "\r\n\r\n", 4) == 0) {
                    end = index + 4;
                    break;
                }
            }
            if (end == 0) {
                if (rxLength - position >= sizeof(rxBuffer) / 2) {
                    setLastError("rtsp_protocol_error");
                    return false;
                }
                break;
            }
            size_t bodyLength = 0;
            const char* lengthText = nullptr;
            for (size_t index = position; index + 15 < end; index++) {
                if (equalsIgnoreCasePrefix(reinterpret_cast<const char*>(rxBuffer + index), "Content-Length:")) {
                    lengthText = reinterpret_cast<const char*>(rxBuffer + index + 15);
                    break;
                }
            }
            if (lengthText != nullptr)
                bodyLength = static_cast<size_t>(strtoul(lengthText, nullptr, 10));
            if (rxLength < end + bodyLength)
                break;
            position = end + bodyLength;
        } else {
            position++;
        }
    }
    if (position > 0)
        consumeRx(position);
    return true;
}

void updateRates(unsigned long now)
{
    if (now - rateWindowStartedAtMs < 1000UL)
        return;
    const unsigned long elapsed = now - rateWindowStartedAtMs;
    statFps = static_cast<uint16_t>((static_cast<unsigned long>(rateFrames) * 1000UL + elapsed / 2) / elapsed);
    statKbps = static_cast<uint32_t>(((fromCameraTotal - rateBytes) * 8ULL) / elapsed);
    rateFrames = 0;
    rateBytes = fromCameraTotal;
    rateWindowStartedAtMs = now;
}

void handleSessionFailure()
{
    statSessionFailures = statSessionFailures + 1;
    if (consecutiveFailures < 255)
        consecutiveFailures++;
    const uint32_t backoff = min<uint32_t>(RETRY_MAX_MS, RETRY_BASE_MS << min<uint8_t>(consecutiveFailures - 1, 3));
    retryAtMs = millis() + backoff;
    const char* error = lastErrorText[0] != '\0' ? lastErrorText : "unknown";
    Serial.printf("[live] RTSP session failure: %s (attempt %u)\n", error, static_cast<unsigned>(consecutiveFailures));
    stopSession(LiveStreamState::Retrying, error);
    if (consecutiveFailures >= FAILURES_BEFORE_NOTIFYING_CLIENTS) {
        stateValue = LiveStreamState::Failed;
        closeAllClients(error);
        consecutiveFailures = 0;
    }
}

bool openListenSocket()
{
    const int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    const int enabled = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(LIVE_STREAM_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lwip_bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || lwip_listen(fd, 2) < 0) {
        lwip_close(fd);
        return false;
    }
    lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    listenFd = fd;
    return true;
}

void liveTask(void*)
{
    while (!openListenSocket()) {
        Serial.println("[live] could not open the relay port; trying again");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    Serial.printf("[live] video relay listening on port %u\n", static_cast<unsigned>(LIVE_STREAM_PORT));

    while (true) {
        const unsigned long now = millis();
        const size_t viewers = clientCount();
        statClientCount = static_cast<uint8_t>(viewers);

        if (viewers > 0)
            idleSinceMs = 0;
        else if (idleSinceMs == 0)
            idleSinceMs = now;

        uint32_t currentGeneration = 0;
        taskENTER_CRITICAL(&targetMux);
        currentGeneration = publishedGeneration;
        taskEXIT_CRITICAL(&targetMux);

        if (session.active && session.generation != currentGeneration)
            stopSession(LiveStreamState::Idle, nullptr);

        if (session.active && viewers == 0 && idleSinceMs != 0 && now - idleSinceMs >= SESSION_IDLE_GRACE_MS) {
            stopSession(LiveStreamState::Idle, nullptr);
            consecutiveFailures = 0;
        }

        if (!session.active && viewers > 0 && static_cast<long>(now - retryAtMs) >= 0) {
            if (startSession()) {
                consecutiveFailures = 0;
            } else {
                handleSessionFailure();
            }
        } else if (!session.active && viewers == 0 && stateValue != LiveStreamState::Idle) {
            stateValue = LiveStreamState::Idle;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        int highest = listenFd;
        FD_SET(listenFd, &readSet);
        if (session.active) {
            FD_SET(session.fd, &readSet);
            highest = max(highest, session.fd);
        }
        for (const Client& client : clients) {
            if (client.fd >= 0) {
                FD_SET(client.fd, &readSet);
                highest = max(highest, client.fd);
            }
        }

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 40000;
        const int ready = lwip_select(highest + 1, &readSet, nullptr, nullptr, &timeout);

        if (ready > 0) {
            if (FD_ISSET(listenFd, &readSet))
                acceptClient();
            for (Client& client : clients) {
                if (client.fd >= 0 && FD_ISSET(client.fd, &readSet))
                    readClient(client);
            }
            if (session.active && FD_ISSET(session.fd, &readSet) && !pumpRtsp())
                handleSessionFailure();
        } else if (ready < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (session.active) {
            const unsigned long current = millis();
            if (current - session.lastKeepaliveAtMs >= session.keepaliveIntervalMs) {
                session.lastKeepaliveAtMs = current;
                sendKeepalive();
            }
            if (session.lastPacketAtMs == 0 || current - session.lastPacketAtMs >= RTSP_STALL_TIMEOUT_MS) {
                setLastError(session.lastPacketAtMs == 0 ? "camera_stream_closed" : "camera_stream_stalled");
                handleSessionFailure();
            } else {
                updateRates(current);
            }
        }

        vTaskDelay(1);
    }
}

}

const char* liveStreamStateName(LiveStreamState state)
{
    switch (state) {
        case LiveStreamState::Idle:
            return "idle";
        case LiveStreamState::Connecting:
            return "connecting";
        case LiveStreamState::Streaming:
            return "streaming";
        case LiveStreamState::Retrying:
            return "retrying";
        case LiveStreamState::Failed:
            return "failed";
    }
    return "idle";
}

bool liveStreamBegin()
{
    if (taskHandle != nullptr)
        return true;
    const BaseType_t created = xTaskCreatePinnedToCore(liveTask, "live-relay", TASK_STACK_BYTES, nullptr, 2, &taskHandle, 0);
    if (created != pdPASS) {
        taskHandle = nullptr;
        Serial.println("[live] failed to create the video relay task");
        return false;
    }
    return true;
}

void liveStreamSetTarget(const LiveStreamTarget& target)
{
    taskENTER_CRITICAL(&targetMux);
    const bool changed = !publishedTarget.valid || !target.valid || publishedTarget.cameraId != target.cameraId || memcmp(publishedTarget.ip, target.ip, 4) != 0 || publishedTarget.port != target.port || publishedTarget.highQuality != target.highQuality || strcmp(publishedTarget.path, target.path) != 0 || strcmp(publishedTarget.username, target.username) != 0 || strcmp(publishedTarget.password, target.password) != 0;
    publishedTarget = target;
    if (changed)
        publishedGeneration++;
    taskEXIT_CRITICAL(&targetMux);
}

bool liveStreamCopyTarget(LiveStreamTarget& target)
{
    taskENTER_CRITICAL(&targetMux);
    target = publishedTarget;
    taskEXIT_CRITICAL(&targetMux);
    return target.valid;
}

LiveStreamStats liveStreamStats()
{
    LiveStreamStats stats;
    stats.state = stateValue;
    stats.lastError = lastErrorText;
    stats.clients = statClientCount;
    stats.framesPerSecond = statFps;
    stats.kilobitsPerSecond = statKbps;
    stats.sessionsStarted = statSessionsStarted;
    stats.sessionFailures = statSessionFailures;
    stats.framesRelayed = statFrames;
    stats.packetsRelayed = statPackets;
    stats.packetsDropped = statDropped;
    stats.clientsServed = statClientsServed;
    stats.clientsRejected = statClientsRejected;
    stats.bytesFromCamera = (static_cast<uint64_t>(statBytesFromCameraHigh) << 32) | statBytesFromCameraLow;
    stats.bytesToClients = (static_cast<uint64_t>(statBytesToClientsHigh) << 32) | statBytesToClientsLow;
    const unsigned long lastFrameAt = statLastFrameAtMs;
    stats.lastFrameAgeMs = lastFrameAt == 0 ? UINT32_MAX : static_cast<uint32_t>(millis() - lastFrameAt);
    stats.sessionAgeMs = stats.state == LiveStreamState::Streaming ? static_cast<uint32_t>(millis() - statSessionStartedAtMs) : 0;

    taskENTER_CRITICAL(&targetMux);
    stats.cameraId = publishedTarget.cameraId;
    stats.width = publishedTarget.width;
    stats.height = publishedTarget.height;
    memcpy(stats.profileName, publishedTarget.profileName, sizeof(stats.profileName));
    taskEXIT_CRITICAL(&targetMux);
    return stats;
}

bool liveStreamDeliveredRecently(uint32_t cameraId, uint32_t windowMs, uint32_t* framesInWindow)
{
    if (framesInWindow != nullptr)
        *framesInWindow = 0;
    if (stateValue != LiveStreamState::Streaming || statLastFrameCameraId != cameraId)
        return false;
    const unsigned long lastFrameAt = statLastFrameAtMs;
    if (lastFrameAt == 0 || millis() - lastFrameAt > windowMs)
        return false;
    if (framesInWindow != nullptr)
        *framesInWindow = statFps;
    return true;
}

}
