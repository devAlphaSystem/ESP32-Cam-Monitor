#include "firmware_internal.h"
#include "camera_policy.h"

namespace CameraWatchdog {

constexpr char NVS_NAMESPACE[] = "camwatch";
constexpr unsigned long ONVIF_CATALOG_FALLBACK_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long ONVIF_PTZ_PRESET_REFRESH_INTERVAL_MS = 60UL * 1000UL;
constexpr unsigned long ONVIF_PTZ_PRESET_RETRY_INTERVAL_MS = 15UL * 1000UL;
constexpr unsigned long CAMERA_CLOCK_CHECK_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long CAMERA_CLOCK_RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t CAMERA_CLOCK_MAX_ROUND_TRIP_MS = 2000UL;

const IPAddress WS_DISCOVERY_MULTICAST(239, 255, 255, 250);
constexpr size_t DISCOVERY_IDENTITY_QUEUE_CAPACITY = 8;
CameraTarget discoveryIdentityQueue[DISCOVERY_IDENTITY_QUEUE_CAPACITY];
size_t discoveryIdentityQueueCount = 0;
uint32_t nextDiscoveryMatchGroup = 1;
uint32_t lastUnconfirmedDiscoveryGroup = 0;

constexpr char SOAP_GET_SYSTEM_TIME[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" " "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">" "<s:Body><tds:GetSystemDateAndTime/></s:Body>" "</s:Envelope>";

bool postOnvifSoap(CameraContext& ctx, const String& serviceUrl, const char* action, const String& body, String& responseBody, bool requireResponseBody = true, int* finalStatusCode = nullptr, bool* transportFailure = nullptr, uint32_t timeoutMs = 0, uint16_t contextId = 0);

uint32_t ipToUint32(const IPAddress& ip)
{
    return (static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) | (static_cast<uint32_t>(ip[2]) << 8) | static_cast<uint32_t>(ip[3]);
}

IPAddress uint32ToIp(uint32_t value)
{
    return IPAddress(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
}

bool parseHttpResourceUrl(const String& url, ParsedHttpResource& parsed)
{
    parsed = ParsedHttpResource{};

    String normalizedUrl = url;
    normalizedUrl.trim();

    int hostStart = -1;

    if (normalizedUrl.startsWith("http://")) {
        parsed.secure = false;
        parsed.port = 80;
        hostStart = 7;
    } else if (normalizedUrl.startsWith("https://")) {
        parsed.secure = true;
        parsed.port = 443;
        hostStart = 8;
    } else {
        return false;
    }

    const int pathStart = normalizedUrl.indexOf('/', hostStart);
    const int hostEnd = pathStart >= 0 ? pathStart : normalizedUrl.length();
    String authority = normalizedUrl.substring(hostStart, hostEnd);
    parsed.path = pathStart >= 0 ? normalizedUrl.substring(pathStart) : "/";

    if (authority.isEmpty() || authority.indexOf('@') >= 0 || parsed.path.indexOf('\r') >= 0 || parsed.path.indexOf('\n') >= 0) {
        return false;
    }

    const int colonPosition = authority.lastIndexOf(':');

    if (colonPosition > 0) {
        const String portText = authority.substring(colonPosition + 1);

        if (portText.isEmpty()) {
            return false;
        }

        for (size_t index = 0; index < portText.length(); index++) {
            if (!isdigit(static_cast<unsigned char>(portText.charAt(index)))) {
                return false;
            }
        }

        const long parsedPort = portText.toInt();

        if (parsedPort <= 0 || parsedPort > 65535) {
            return false;
        }

        parsed.port = static_cast<uint16_t>(parsedPort);
        authority = authority.substring(0, colonPosition);
    }

    if (authority.isEmpty()) {
        return false;
    }

    parsed.host = authority;
    parsed.valid = true;
    return true;
}

bool parseHttpUrl(const String& url, ParsedUrl& parsed)
{
    parsed = ParsedUrl{};
    ParsedHttpResource resource;

    if (!parseHttpResourceUrl(url, resource)) {
        return false;
    }

    if (!parsed.ip.fromString(resource.host.c_str())) {
        return false;
    }

    parsed.valid = true;
    parsed.secure = resource.secure;
    parsed.port = resource.port;
    parsed.path = resource.path;
    return true;
}

String extractDigestParameter(const String& challenge, const char* name)
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

    while (valueStart < static_cast<int>(challenge.length()) && isspace(static_cast<unsigned char>(challenge.charAt(valueStart)))) {
        valueStart++;
    }

    if (valueStart >= static_cast<int>(challenge.length())) {
        return String();
    }

    if (challenge.charAt(valueStart) == '"') {
        valueStart++;
        const int valueEnd = challenge.indexOf('"', valueStart);
        return valueEnd >= 0 ? challenge.substring(valueStart, valueEnd) : String();
    }

    int valueEnd = valueStart;

    while (valueEnd < static_cast<int>(challenge.length()) && challenge.charAt(valueEnd) != ',' && !isspace(static_cast<unsigned char>(challenge.charAt(valueEnd)))) {
        valueEnd++;
    }

    String value = challenge.substring(valueStart, valueEnd);
    value.trim();
    return value;
}

bool digestQopSupportsAuth(const String& qopList)
{
    int tokenStart = 0;

    while (tokenStart < static_cast<int>(qopList.length())) {
        const int commaPosition = qopList.indexOf(',', tokenStart);
        const int tokenEnd = commaPosition >= 0 ? commaPosition : static_cast<int>(qopList.length());

        String token = qopList.substring(tokenStart, tokenEnd);
        token.trim();
        token.toLowerCase();

        if (token == "auth") {
            return true;
        }

        if (commaPosition < 0) {
            break;
        }

        tokenStart = commaPosition + 1;
    }

    return false;
}

bool parseDigestChallenge(const String& authenticateHeader, DigestChallenge& challenge)
{
    challenge = DigestChallenge{};

    String normalized = authenticateHeader;
    normalized.toLowerCase();
    const int digestPosition = normalized.indexOf("digest");

    if (digestPosition < 0) {
        return false;
    }

    const String digestText = authenticateHeader.substring(digestPosition + 6);
    challenge.realm = extractDigestParameter(digestText, "realm");
    challenge.nonce = extractDigestParameter(digestText, "nonce");
    challenge.qop = extractDigestParameter(digestText, "qop");
    challenge.opaque = extractDigestParameter(digestText, "opaque");

    const String algorithm = extractDigestParameter(digestText, "algorithm");

    if (!algorithm.isEmpty()) {
        challenge.algorithm = algorithm;
    }

    challenge.valid = !challenge.realm.isEmpty() && !challenge.nonce.isEmpty();
    return challenge.valid;
}

String md5Hex(const String& input)
{
    MD5Builder md5;
    md5.begin();
    md5.add(input);
    md5.calculate();
    return md5.toString();
}

String httpQuotedEscape(const String& input)
{
    String escaped;
    escaped.reserve(input.length() + 8);

    for (size_t index = 0; index < input.length(); index++) {
        const char value = input.charAt(index);

        if (value == '\\' || value == '"') {
            escaped += '\\';
        }

        escaped += value;
    }

    return escaped;
}

String makeDigestAuthorization(CameraContext& ctx, const char* method, const String& url, const DigestChallenge& challenge, uint32_t nonceCountValue = 1)
{
    if (!cameraCredentialsConfigured(ctx) || !challenge.valid) {
        return String();
    }

    ParsedHttpResource resource;

    if (!parseHttpResourceUrl(url, resource)) {
        return String();
    }

    char cnonceBuffer[33];
    snprintf(cnonceBuffer, sizeof(cnonceBuffer), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));

    const String cnonce = cnonceBuffer;
    char nonceCountBuffer[9];
    snprintf(nonceCountBuffer, sizeof(nonceCountBuffer), "%08lx", static_cast<unsigned long>(nonceCountValue));
    const String nonceCount = nonceCountBuffer;

    String ha1 = md5Hex(String(ctx.settings.cameraUsername) + ':' + challenge.realm + ':' + ctx.settings.cameraPassword);

    String normalizedAlgorithm = challenge.algorithm;
    normalizedAlgorithm.toLowerCase();

    if (normalizedAlgorithm == "md5-sess") {
        ha1 = md5Hex(ha1 + ':' + challenge.nonce + ':' + cnonce);
    } else if (normalizedAlgorithm != "md5" && !normalizedAlgorithm.isEmpty()) {
        return String();
    }

    const String ha2 = md5Hex(String(method) + ':' + resource.path);
    String selectedQop;

    if (!challenge.qop.isEmpty()) {
        if (!digestQopSupportsAuth(challenge.qop)) {
            return String();
        }

        selectedQop = "auth";
    }

    const String response = selectedQop.isEmpty() ? md5Hex(ha1 + ':' + challenge.nonce + ':' + ha2) : md5Hex(ha1 + ':' + challenge.nonce + ':' + nonceCount + ':' + cnonce + ':' + selectedQop + ':' + ha2);

    String authorization;
    authorization.reserve(512);
    authorization += "username=\"";
    authorization += httpQuotedEscape(ctx.settings.cameraUsername);
    authorization += "\", realm=\"";
    authorization += httpQuotedEscape(challenge.realm);
    authorization += "\", nonce=\"";
    authorization += httpQuotedEscape(challenge.nonce);
    authorization += "\", uri=\"";
    authorization += httpQuotedEscape(resource.path);
    authorization += "\", response=\"";
    authorization += response;
    authorization += '"';

    if (!challenge.algorithm.isEmpty()) {
        authorization += ", algorithm=";
        authorization += challenge.algorithm;
    }

    if (!selectedQop.isEmpty()) {
        authorization += ", qop=";
        authorization += selectedQop;
        authorization += ", nc=";
        authorization += nonceCount;
        authorization += ", cnonce=\"";
        authorization += cnonce;
        authorization += '"';
    }

    if (!challenge.opaque.isEmpty()) {
        authorization += ", opaque=\"";
        authorization += httpQuotedEscape(challenge.opaque);
        authorization += '"';
    }

    return authorization;
}

void applyCameraAuthorization(CameraContext& ctx, HTTPClient& request, const String& digestAuthorization)
{
    if (!digestAuthorization.isEmpty()) {
        request.setAuthorizationType("Digest");
        request.setAuthorization(digestAuthorization.c_str());
        return;
    }

    request.setAuthorizationType("Basic");
    if (cameraCredentialsConfigured(ctx)) {
        request.setAuthorization(ctx.settings.cameraUsername, ctx.settings.cameraPassword);
    } else {
        request.setAuthorization("");
    }
}

void finishCameraSnapshotGet(CameraContext& ctx, bool responseCanBeReused)
{
    ctx.cameraSnapshotHttpSession.request.end();

    if (responseCanBeReused && ctx.cameraSnapshotHttpSession.client.connected()) {
        ctx.cameraSnapshotHttpSession.reusableAtMs = millis();
    } else {
        ctx.cameraSnapshotHttpSession.client.stop();
        ctx.cameraSnapshotHttpSession.reusableAtMs = 0;
    }
}

void maintainCameraSnapshotHttpSession(CameraContext& ctx)
{
    if (ctx.cameraSnapshotHttpSession.reusableAtMs != 0 && millis() - ctx.cameraSnapshotHttpSession.reusableAtMs >= SNAPSHOT_CONNECTION_REUSE_IDLE_MS) {
        finishCameraSnapshotGet(ctx, false);
    }
}

void resetCameraSnapshotHttpSession(CameraContext& ctx)
{
    finishCameraSnapshotGet(ctx, false);
    ctx.cameraSnapshotHttpSession.url = String();
    ctx.cameraSnapshotHttpSession.digestChallenge = DigestChallenge{};
    ctx.cameraSnapshotHttpSession.digestNonceCount = 0;
    ctx.cameraSnapshotHttpSession.forceHttp10 = false;
}

String makeNextCameraSnapshotDigestAuthorization(CameraContext& ctx, const String& url)
{
    CameraSnapshotHttpSession& session = ctx.cameraSnapshotHttpSession;

    if (!session.digestChallenge.valid) {
        return String();
    }

    const uint32_t nextNonceCount = session.digestNonceCount == 0xFFFFFFFFUL ? 1UL : session.digestNonceCount + 1UL;
    const String authorization = makeDigestAuthorization(ctx, "GET", url, session.digestChallenge, nextNonceCount);

    if (authorization.isEmpty()) {
        session.digestChallenge = DigestChallenge{};
        session.digestNonceCount = 0;
        return String();
    }

    session.digestNonceCount = nextNonceCount;
    return authorization;
}

int openAuthenticatedCameraGet(CameraContext& ctx, const String& url, uint16_t contextId = 0)
{
    CameraSnapshotHttpSession& session = ctx.cameraSnapshotHttpSession;
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    ParsedUrl logEndpoint;
    parseHttpUrl(url, logEndpoint);

    if (session.url != url) {
        resetCameraSnapshotHttpSession(ctx);
        session.url = url;
    }

    HTTPClient& request = session.request;
    WiFiClient& client = session.client;
    const char* responseHeaders[] = {"Content-Type", "WWW-Authenticate", "Transfer-Encoding"};

    request.setConnectTimeout(ctx.settings.tcpConnectTimeoutMs);
    request.setTimeout(ctx.settings.snapshotHttpTimeoutMs);
    request.useHTTP10(session.forceHttp10);
    request.setReuse(!session.forceHttp10);
    request.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    request.setUserAgent(String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION);
    request.collectHeaders(responseHeaders, 3);

    bool responseUsesChunkedTransfer = false;

    const auto executeAttempt = [&](const String& digestAuthorization, bool authenticationRetry) {
        responseUsesChunkedTransfer = false;
        const unsigned long attemptStartedAtMs = millis();

        if (!request.begin(client, url)) {
            PersistentLog::record(PersistentLog::Event::SnapshotHttp, PersistentLog::Outcome::Failure, "camera_jpeg", logEndpoint.ip, logEndpoint.port, -1, millis() - attemptStartedAtMs, 0, static_cast<uint8_t>(PersistentLog::FlagOutbound | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), PersistentLog::Level::Automatic, false, logContextId, ctx.id);
            return -1;
        }

        const bool connectionWasOpen = client.connected();
        ctx.cameraJpegHttpRequests++;
        request.addHeader("Accept", "image/jpeg, image/*;q=0.9, */*;q=0.1");
        applyCameraAuthorization(ctx, request, digestAuthorization);
        const int statusCode = request.GET();
        const bool requestAccepted = statusCode >= 200 && statusCode < 400;
        const unsigned long attemptFinishedAtMs = millis();
        const bool logAttempt = !requestAccepted || ctx.lastSuccessfulSnapshotHttpLogAtMs == 0 || attemptFinishedAtMs - ctx.lastSuccessfulSnapshotHttpLogAtMs >= SNAPSHOT_SUCCESS_LOG_INTERVAL_MS;

        if (logAttempt) {
            PersistentLog::record(PersistentLog::Event::SnapshotHttp, requestAccepted ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, "camera_jpeg", logEndpoint.ip, logEndpoint.port, statusCode, attemptFinishedAtMs - attemptStartedAtMs, statusCode > 0 ? static_cast<uint32_t>(max(request.getSize(), 0)) : 0, static_cast<uint8_t>(PersistentLog::FlagOutbound | (connectionWasOpen ? PersistentLog::FlagConnectionReused : PersistentLog::FlagNone) | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), requestAccepted || (statusCode == 401 && !authenticationRetry && cameraCredentialsConfigured(ctx)) ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, logContextId, ctx.id);

            if (requestAccepted) {
                ctx.lastSuccessfulSnapshotHttpLogAtMs = attemptFinishedAtMs;
            }
        }

        if (connectionWasOpen && statusCode > 0) {
            ctx.cameraJpegConnectionReuses++;
        }

        String transferEncoding = request.header("Transfer-Encoding");
        transferEncoding.toLowerCase();

        if (transferEncoding.indexOf("chunked") >= 0) {
            responseUsesChunkedTransfer = true;
            session.forceHttp10 = true;
            request.useHTTP10(true);
        }

        return statusCode;
    };

    String digestAuthorization = makeNextCameraSnapshotDigestAuthorization(ctx, url);

    if (!digestAuthorization.isEmpty()) {
        ctx.cameraJpegDigestPreemptiveRequests++;
    }

    int statusCode = executeAttempt(digestAuthorization, !digestAuthorization.isEmpty());

    if (responseUsesChunkedTransfer && statusCode != 401) {
        finishCameraSnapshotGet(ctx, false);
        digestAuthorization = makeNextCameraSnapshotDigestAuthorization(ctx, url);
        statusCode = executeAttempt(digestAuthorization, !digestAuthorization.isEmpty());
    }

    if (statusCode != 401 || !cameraCredentialsConfigured(ctx)) {
        return statusCode;
    }

    DigestChallenge challenge;
    const String authenticateHeader = request.header("WWW-Authenticate");

    if (!parseDigestChallenge(authenticateHeader, challenge)) {
        session.digestChallenge = DigestChallenge{};
        session.digestNonceCount = 0;
        return statusCode;
    }

    session.digestChallenge = challenge;
    session.digestNonceCount = 0;
    digestAuthorization = makeNextCameraSnapshotDigestAuthorization(ctx, url);

    if (digestAuthorization.isEmpty()) {
        session.digestChallenge = DigestChallenge{};
        session.digestNonceCount = 0;
        return statusCode;
    }

    finishCameraSnapshotGet(ctx, false);
    statusCode = executeAttempt(digestAuthorization, true);

    if (statusCode == 401) {
        session.digestChallenge = DigestChallenge{};
        session.digestNonceCount = 0;
    }

    return statusCode;
}

String makeOnvifUrl(const IPAddress& ip, uint16_t port, const String& path)
{
    String url = "http://";
    url += ip.toString();

    if (port != 80) {
        url += ':';
        url += port;
    }

    if (!path.startsWith("/")) {
        url += '/';
    }

    url += path;
    return url;
}

bool extractLocalTag(const String& xml, const char* localName, int fromPosition, String& value, int& nextPosition)
{
    const String marker = String(localName) + ">";
    int searchPosition = fromPosition;

    while (true) {
        const int namePosition = xml.indexOf(marker, searchPosition);

        if (namePosition < 0) {
            return false;
        }

        const int openStart = xml.lastIndexOf('<', namePosition);

        if (openStart < 0) {
            return false;
        }

        const char beforeLocalName = namePosition > openStart ? xml.charAt(namePosition - 1) : '\0';

        if (beforeLocalName != '<' && beforeLocalName != ':') {
            searchPosition = namePosition + marker.length();
            continue;
        }

        if (openStart + 1 < static_cast<int>(xml.length()) && xml.charAt(openStart + 1) == '/') {
            searchPosition = namePosition + marker.length();
            continue;
        }

        const int contentStart = namePosition + marker.length();
        const int closeStart = xml.indexOf("</", contentStart);

        if (closeStart < 0) {
            return false;
        }

        const int closeEnd = xml.indexOf('>', closeStart + 2);

        if (closeEnd < 0) {
            return false;
        }

        const String closingTag = xml.substring(closeStart, closeEnd + 1);

        if (closingTag.indexOf(localName) < 0) {
            searchPosition = contentStart;
            continue;
        }

        value = xml.substring(contentStart, closeStart);
        value.trim();
        nextPosition = closeEnd + 1;
        return true;
    }
}

String firstWhitespaceToken(const String& text)
{
    int start = 0;

    while (start < static_cast<int>(text.length()) && isspace(static_cast<unsigned char>(text.charAt(start)))) {
        start++;
    }

    int end = start;

    while (end < static_cast<int>(text.length()) && !isspace(static_cast<unsigned char>(text.charAt(end)))) {
        end++;
    }

    return text.substring(start, end);
}

bool containsIgnoreCase(const String& haystack, const String& needle)
{
    if (needle.isEmpty()) {
        return true;
    }

    String normalizedHaystack = haystack;
    String normalizedNeedle = needle;
    normalizedHaystack.toLowerCase();
    normalizedNeedle.toLowerCase();

    return normalizedHaystack.indexOf(normalizedNeedle) >= 0;
}

String xmlUnescape(String value)
{
    value.replace("&amp;", "&");
    value.replace("&quot;", "\"");
    value.replace("&apos;", "'");
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    return value;
}

bool extractXmlAttribute(const String& openingTag, const char* attributeName, String& value)
{
    const String doubleQuoteMarker = String(attributeName) + "=\"";
    int valueStart = openingTag.indexOf(doubleQuoteMarker);
    char closingQuote = '\"';

    if (valueStart >= 0) {
        valueStart += doubleQuoteMarker.length();
    } else {
        const String singleQuoteMarker = String(attributeName) + "='";
        valueStart = openingTag.indexOf(singleQuoteMarker);

        if (valueStart < 0) {
            return false;
        }

        valueStart += singleQuoteMarker.length();
        closingQuote = '\'';
    }

    const int valueEnd = openingTag.indexOf(closingQuote, valueStart);

    if (valueEnd < 0) {
        return false;
    }

    value = xmlUnescape(openingTag.substring(valueStart, valueEnd));
    return true;
}

bool findNextOpeningTag(const String& xml, const char* localName, int fromPosition, int& openingStart, int& openingEnd)
{
    int searchPosition = fromPosition;
    const size_t localNameLength = strlen(localName);

    while (true) {
        const int namePosition = xml.indexOf(localName, searchPosition);

        if (namePosition < 0) {
            return false;
        }

        const int nameEnd = namePosition + localNameLength;
        const char before = namePosition > 0 ? xml.charAt(namePosition - 1) : '\0';
        const char after = nameEnd < static_cast<int>(xml.length()) ? xml.charAt(nameEnd) : '\0';

        if ((before == '<' || before == ':') && (after == '>' || after == '/' || isspace(static_cast<unsigned char>(after)))) {
            openingStart = xml.lastIndexOf('<', namePosition);

            if (openingStart >= 0 && openingStart + 1 < static_cast<int>(xml.length()) && xml.charAt(openingStart + 1) != '/') {
                openingEnd = xml.indexOf('>', nameEnd);
                return openingEnd >= 0;
            }
        }

        searchPosition = nameEnd;
    }
}

void invalidateSnapshotTarget(CameraContext& ctx)
{
    resetCameraSnapshotHttpSession(ctx);
    ctx.snapshotTarget = SnapshotTarget{};
}

bool extractLocalElementContent(const String& xml, const char* localName, int fromPosition, String& value, int& nextPosition)
{
    int openingStart = 0;
    int openingEnd = 0;

    if (!findNextOpeningTag(xml, localName, fromPosition, openingStart, openingEnd)) {
        return false;
    }

    const String closingMarker = String(localName) + '>';
    int searchPosition = openingEnd + 1;

    while (true) {
        const int namePosition = xml.indexOf(closingMarker, searchPosition);

        if (namePosition < 0) {
            return false;
        }

        const int closingStart = xml.lastIndexOf('<', namePosition);
        const char beforeLocalName = namePosition > closingStart ? xml.charAt(namePosition - 1) : '\0';

        if (closingStart >= 0 && closingStart + 1 < static_cast<int>(xml.length()) && xml.charAt(closingStart + 1) == '/' && (beforeLocalName == '/' || beforeLocalName == ':')) {
            const int closingEnd = xml.indexOf('>', namePosition);

            if (closingEnd < 0) {
                return false;
            }

            value = xml.substring(openingEnd + 1, closingStart);
            value.trim();
            nextPosition = closingEnd + 1;
            return true;
        }

        searchPosition = namePosition + closingMarker.length();
    }
}

bool parseOnvifDateNumber(const String& xml, const char* localName, int minimum, int maximum, int& result)
{
    String value;
    int ignored = 0;

    if (!extractLocalTag(xml, localName, 0, value, ignored)) {
        return false;
    }

    value.trim();

    if (value.isEmpty()) {
        return false;
    }

    for (size_t index = 0; index < value.length(); index++) {
        if (!isdigit(static_cast<unsigned char>(value.charAt(index)))) {
            return false;
        }
    }

    const long parsed = value.toInt();

    if (parsed < minimum || parsed > maximum) {
        return false;
    }

    result = static_cast<int>(parsed);
    return true;
}

bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month)
{
    static const uint8_t MONTH_DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2 && isLeapYear(year)) {
        return 29;
    }

    return MONTH_DAYS[month - 1];
}

int64_t civilDaysSinceUnixEpoch(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = static_cast<unsigned>(static_cast<int>(month) + (month > 2 ? -3 : 9));
    const unsigned dayOfYear = (153U * shiftedMonth + 2U) / 5U + day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return static_cast<int64_t>(era) * 146097LL + dayOfEra - 719468LL;
}

bool parseOnvifUtcDateTime(const String& xml, int64_t& unixTimeMs)
{
    String utcDateTime;
    int ignored = 0;

    if (!extractLocalElementContent(xml, "UTCDateTime", 0, utcDateTime, ignored)) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (!parseOnvifDateNumber(utcDateTime, "Year", 1970, 2400, year) || !parseOnvifDateNumber(utcDateTime, "Month", 1, 12, month) || !parseOnvifDateNumber(utcDateTime, "Day", 1, 31, day) || !parseOnvifDateNumber(utcDateTime, "Hour", 0, 23, hour) || !parseOnvifDateNumber(utcDateTime, "Minute", 0, 59, minute) || !parseOnvifDateNumber(utcDateTime, "Second", 0, 60, second) || day > daysInMonth(year, month)) {
        return false;
    }

    const int64_t days = civilDaysSinceUnixEpoch(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    unixTimeMs = (days * 86400LL + static_cast<int64_t>(hour) * 3600LL + static_cast<int64_t>(minute) * 60LL + second) * 1000LL;
    return true;
}

int64_t synchronizedUnixTimeMs()
{
    struct timeval value;

    if (gettimeofday(&value, nullptr) != 0) {
        return 0;
    }

    return static_cast<int64_t>(value.tv_sec) * 1000LL + value.tv_usec / 1000LL;
}

void resetSnapshotProfileFailover(CameraContext& ctx)
{
    ctx.snapshotProfileTokenToSkip = String();
    ctx.snapshotProfileFailoverPending = false;
}

struct TcpConnectionAttempt {
    bool connected = false;
    uint32_t durationMs = 0;
    int errorCode = 0;
    const char* detail = "connected";
};

const char* tcpConnectionErrorDetail(int errorCode)
{
    switch (errorCode) {
        case ECONNREFUSED:
            return "connection_refused";
        case ECONNRESET:
            return "connection_reset";
        case ENETUNREACH:
            return "network_unreachable";
        case EHOSTUNREACH:
            return "host_unreachable";
        case ETIMEDOUT:
            return "connect_timeout";
        default:
            return "connect_failed";
    }
}

TcpConnectionAttempt connectTcp(WiFiClient& client, const IPAddress& ip, uint16_t port, uint32_t timeoutMs)
{
    TcpConnectionAttempt attempt;
    const unsigned long startedAtMs = millis();
    errno = 0;
    attempt.connected = client.connect(ip, port, timeoutMs);
    attempt.durationMs = millis() - startedAtMs;
    attempt.errorCode = attempt.connected ? 0 : errno;

    if (!attempt.connected) {
        const bool frameworkErrorUnavailable = attempt.errorCode == 0 || attempt.errorCode == EINPROGRESS || attempt.errorCode == EAGAIN;

        if (frameworkErrorUnavailable && attempt.durationMs + 5 >= timeoutMs) {
            attempt.errorCode = ETIMEDOUT;
        } else if (frameworkErrorUnavailable) {
            attempt.errorCode = -1;
        }

        attempt.detail = tcpConnectionErrorDetail(attempt.errorCode);
    }

    return attempt;
}

bool loadCameraTarget(CameraContext& ctx)
{
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Skipped, "nvs_open_failed", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info);
        return false;
    }

    const String savedUrl = preferences.getString("cam_url", "");
    const String savedUuid = preferences.getString("cam_uuid", "");
    const String savedScopes = preferences.getString("cam_scope", "");
    preferences.end();

    if (savedUrl.isEmpty()) {
        PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Skipped, "target_not_saved", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info);
        return false;
    }

    ParsedUrl parsed;

    if (!parseHttpUrl(savedUrl, parsed) || parsed.secure) {
        PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Failure, "saved_target_invalid", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Error, true);
        return false;
    }

    ctx.camera.valid = true;
    ctx.camera.ip = parsed.ip;
    ctx.camera.onvifPort = parsed.port;
    ctx.camera.rtspPort = ctx.settings.cameraRtspPort;
    ctx.camera.onvifPath = parsed.path;
    ctx.camera.onvifUrl = savedUrl;
    ctx.camera.endpointUuid = savedUuid;
    ctx.camera.scopes = savedScopes;

    Serial.printf("Camera carregada da NVS: %s:%u\n", ctx.camera.ip.toString().c_str(), ctx.camera.onvifPort);
    PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Success, "nvs_target", ctx.camera.ip, ctx.camera.onvifPort, 1, 0, ctx.camera.rtspPort, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
    return true;
}

bool loadCameraIpHint(CameraContext& ctx)
{
    if (String(CAMERA_IP_HINT).isEmpty()) {
        PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Skipped, "ip_hint_not_configured", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info);
        return false;
    }

    IPAddress hintedIp;

    if (!hintedIp.fromString(CAMERA_IP_HINT)) {
        Serial.println("CAMERA_IP_HINT invalido.");
        PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Failure, "ip_hint_invalid", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true);
        return false;
    }

    ctx.camera.valid = true;
    ctx.camera.ip = hintedIp;
    ctx.camera.onvifPort = ONVIF_PORT_CANDIDATES[0];
    ctx.camera.rtspPort = ctx.settings.cameraRtspPort;
    ctx.camera.onvifPath = DEFAULT_ONVIF_PATH;
    ctx.camera.onvifUrl = makeOnvifUrl(ctx.camera.ip, ctx.camera.onvifPort, ctx.camera.onvifPath);

    Serial.printf("Usando CAMERA_IP_HINT: %s\n", ctx.camera.ip.toString().c_str());
    PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Success, "ip_hint", ctx.camera.ip, ctx.camera.onvifPort, 1, 0, ctx.camera.rtspPort, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
    return true;
}

String makeMessageUuid()
{
    char buffer[64];
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    const uint32_t c = esp_random();
    const uint32_t d = esp_random();

    snprintf(buffer, sizeof(buffer), "uuid:%08lx-%04lx-%04lx-%04lx-%04lx%08lx", static_cast<unsigned long>(a), static_cast<unsigned long>((b >> 16) & 0xFFFF), static_cast<unsigned long>(b & 0xFFFF), static_cast<unsigned long>((c >> 16) & 0xFFFF), static_cast<unsigned long>(c & 0xFFFF), static_cast<unsigned long>(d));

    return String(buffer);
}

bool sendDiscoveryProbe(const char* typeName, uint16_t contextId, uint8_t flags)
{
    const unsigned long startedAtMs = millis();
    String xml;
    xml.reserve(900);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    xml += "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" ";
    xml += "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" ";
    xml += "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" ";
    xml += "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" ";
    xml += "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">";
    xml += "<e:Header>";
    xml += "<w:MessageID>";
    xml += makeMessageUuid();
    xml += "</w:MessageID>";
    xml += "<w:To e:mustUnderstand=\"true\">";
    xml += "urn:schemas-xmlsoap-org:ws:2005:04:discovery";
    xml += "</w:To>";
    xml += "<w:Action e:mustUnderstand=\"true\">";
    xml += "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe";
    xml += "</w:Action>";
    xml += "</e:Header><e:Body><d:Probe>";

    if (typeName != nullptr && strlen(typeName) > 0) {
        xml += "<d:Types>";
        xml += typeName;
        xml += "</d:Types>";
    }

    xml += "</d:Probe></e:Body></e:Envelope>";

    if (!discoveryUdp.beginPacket(WS_DISCOVERY_MULTICAST, WS_DISCOVERY_PORT)) {
        PersistentLog::record(PersistentLog::Event::DiscoveryProbe, PersistentLog::Outcome::Failure, typeName == nullptr ? "any" : typeName, WS_DISCOVERY_MULTICAST, WS_DISCOVERY_PORT, -1, millis() - startedAtMs, static_cast<uint32_t>(xml.length()), static_cast<uint8_t>(PersistentLog::FlagOutbound | flags), PersistentLog::Level::Error, false, contextId);
        return false;
    }

    const size_t written = discoveryUdp.write(reinterpret_cast<const uint8_t*>(xml.c_str()), xml.length());
    const bool sent = written == xml.length() && discoveryUdp.endPacket() == 1;
    PersistentLog::record(PersistentLog::Event::DiscoveryProbe, sent ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, typeName == nullptr ? "any" : typeName, WS_DISCOVERY_MULTICAST, WS_DISCOVERY_PORT, sent ? 1 : 0, millis() - startedAtMs, static_cast<uint32_t>(written), static_cast<uint8_t>(PersistentLog::FlagOutbound | flags), sent ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, contextId);
    return sent;
}

void enqueueDiscoveryIdentityCandidate(const CameraTarget& target)
{
    for (size_t index = 0; index < discoveryIdentityQueueCount; index++)
        if (discoveryIdentityQueue[index].onvifUrl == target.onvifUrl)
            return;
    if (discoveryIdentityQueueCount < DISCOVERY_IDENTITY_QUEUE_CAPACITY)
        discoveryIdentityQueue[discoveryIdentityQueueCount++] = target;
    else
        lastRegistryResult = "Fila de identificacao cheia; candidato sera reavaliado na proxima descoberta";
}

void addDiscoveryResponse(const String& xml)
{
    int position = 0;
    String match;
    int next = 0;
    while (extractLocalElementContent(xml, "ProbeMatch", position, match, next)) {
        position = next;
        const uint32_t discoveryGroup = nextDiscoveryMatchGroup++;
        String xaddrs;
        String uuid;
        String scopes;
        String types;
        int ignored = 0;
        if (!extractLocalTag(match, "XAddrs", 0, xaddrs, ignored))
            continue;
        extractLocalTag(match, "Address", 0, uuid, ignored);
        extractLocalTag(match, "Scopes", 0, scopes, ignored);
        extractLocalTag(match, "Types", 0, types, ignored);
        const bool expectedType = containsIgnoreCase(types, "NetworkVideoTransmitter") || containsIgnoreCase(scopes, "onvif");
        int tokenStart = 0;
        uint8_t examined = 0;
        while (tokenStart < static_cast<int>(xaddrs.length()) && examined < CAMERA_ALTERNATE_ENDPOINT_CAPACITY + 1) {
            while (tokenStart < static_cast<int>(xaddrs.length()) && isspace(static_cast<unsigned char>(xaddrs[tokenStart])))
                tokenStart++;
            if (tokenStart >= static_cast<int>(xaddrs.length()))
                break;
            int tokenEnd = tokenStart;
            while (tokenEnd < static_cast<int>(xaddrs.length()) && !isspace(static_cast<unsigned char>(xaddrs[tokenEnd])))
                tokenEnd++;
            const String url = xmlUnescape(xaddrs.substring(tokenStart, tokenEnd));
            tokenStart = tokenEnd;
            examined++;
            ParsedUrl parsed;
            if (!parseHttpUrl(url, parsed) || parsed.secure || parsed.ip == WiFi.localIP() || !CameraPolicy::safeHttpPath(parsed.path.c_str(), 160, false) || (!expectedType && !containsIgnoreCase(parsed.path, "onvif")))
                continue;
            CameraTarget target;
            target.valid = true;
            target.ip = parsed.ip;
            target.onvifPort = parsed.port;
            target.onvifPath = parsed.path;
            target.onvifUrl = url;
            target.endpointUuid = uuid;
            target.scopes = limitedOnvifText(scopes, 512);
            target.discoveryGroup = discoveryGroup;
            enqueueDiscoveryIdentityCandidate(target);
        }
    }
}

void maintainDiscoveryIdentityCandidates()
{
    if (discoveryIdentityQueueCount == 0 || registryBusy || anyCameraHealthCheckDue() || currentAdminAction() != AdminAction::None || WiFi.status() != WL_CONNECTED || ESP.getFreeHeap() < sizeof(CameraContext) + MAX_ONVIF_RESPONSE_SIZE + CAMERA_OPERATION_HEAP_RESERVE || ESP.getMaxAllocHeap() < MAX_ONVIF_RESPONSE_SIZE + 4096)
        return;
    CameraIoGuard ioGuard;
    if (!ioGuard.acquired())
        return;
    CameraTarget target = discoveryIdentityQueue[0];
    for (size_t index = 1; index < discoveryIdentityQueueCount; index++)
        discoveryIdentityQueue[index - 1] = std::move(discoveryIdentityQueue[index]);
    discoveryIdentityQueue[--discoveryIdentityQueueCount] = CameraTarget{};
    if (target.discoveryGroup != 0 && target.discoveryGroup == lastUnconfirmedDiscoveryGroup)
        return;
    std::unique_ptr<CameraContext> candidate(new (std::nothrow) CameraContext());
    if (!candidate)
        return;
    candidate->camera = target;
    bool identityConflict = false;
    if (!probeCameraIdentity(*candidate, target, 0, &identityConflict)) {
        if (identityConflict)
            return;
        const ServiceProbe serviceProbe = probeOnvif(*candidate, target);
        if (!serviceProbe.protocolResponded || (!serviceProbe.soapResponse && target.discoveryGroup == 0))
            return;
        target.endpointUuid = String();
        target.serialNumber = String();
        target.manufacturer = String();
        target.model = String();
        target.interfaceMacs = String();
    }
    String error;
    CameraContext* ctx = registerCameraTarget(target, error);
    if (ctx == nullptr) {
        lastRegistryResult = error;
        return;
    }
    if (target.discoveryGroup != 0 && !cameraIdentityMatches(target, target))
        lastUnconfirmedDiscoveryGroup = target.discoveryGroup;
    if (ctx->lastDiscoveryGeneration != discoveryRuns) {
        ctx->lastDiscoveryGeneration = discoveryRuns;
        discoveredCount++;
    }
    if (discoveryIdentityQueueCount == 0)
        lastDiscoveryResult = currentTimestamp() + " - cameras identificadas=" + String(discoveredCount) + "; cadastradas=" + String(cameraCount);
}

bool discoverOnvifCameras(bool manualRequest, uint16_t contextId)
{
    const unsigned long startedAtMs = millis();
    CameraIoGuard ioGuard;
    if (!ioGuard.acquired() || registryBusy)
        return false;
    struct DiscoveryGuard {
        DiscoveryGuard()
        {
            registryBusy = true;
        }
        ~DiscoveryGuard()
        {
            registryBusy = false;
        }
    } discoveryGuard;
    discoveryRequested = false;
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    const uint8_t logFlags = manualRequest ? PersistentLog::FlagManual : PersistentLog::FlagScheduled;
    discoveryRuns++;
    lastRediscoveryAtMs = millis();
    lastDiscoveryResult = currentTimestamp() + " - Executando ONVIF WS-Discovery";
    Serial.println(lastDiscoveryResult);
    discoveredCount = 0;

    discoveryUdp.stop();

    if (!discoveryUdp.begin(WS_DISCOVERY_LOCAL_PORT)) {
        lastDiscoveryResult = "Falha ao abrir socket UDP de descoberta";
        PersistentLog::record(PersistentLog::Event::DiscoveryResult, PersistentLog::Outcome::Failure, "udp_socket_failed", WS_DISCOVERY_MULTICAST, WS_DISCOVERY_PORT, -1, millis() - startedAtMs, 0, logFlags, PersistentLog::Level::Error, true, logContextId);
        return false;
    }

    while (discoveryUdp.parsePacket() > 0) {
        while (discoveryUdp.available()) {
            discoveryUdp.read();
        }
    }

    const bool sentDevice = sendDiscoveryProbe("tds:Device", logContextId, logFlags);
    delay(40);
    const bool sentNvt = sendDiscoveryProbe("dn:NetworkVideoTransmitter", logContextId, logFlags);
    delay(40);
    const bool sentAny = sendDiscoveryProbe(nullptr, logContextId, logFlags);

    if (!sentDevice && !sentNvt && !sentAny) {
        lastDiscoveryResult = "Nao foi possivel enviar o Probe multicast";
        discoveryUdp.stop();
        PersistentLog::record(PersistentLog::Event::DiscoveryResult, PersistentLog::Outcome::Failure, "all_probes_failed", WS_DISCOVERY_MULTICAST, WS_DISCOVERY_PORT, 0, millis() - startedAtMs, 0, logFlags, PersistentLog::Level::Error, true, logContextId);
        return false;
    }

    const unsigned long startedAt = millis();

    while (millis() - startedAt < globalSettings.discoveryTimeoutMs) {
        updateCameraAlertLed();
        httpServer.handleClient();

        const int packetSize = discoveryUdp.parsePacket();

        if (packetSize <= 0) {
            delay(2);
            continue;
        }

        const IPAddress remoteIp = discoveryUdp.remoteIP();
        const uint16_t remotePort = discoveryUdp.remotePort();
        const bool responseTruncated = static_cast<size_t>(packetSize) > WS_DISCOVERY_BUFFER_SIZE;
        const size_t candidateCountBefore = discoveredCount;

        const size_t bytesToRead = min(static_cast<size_t>(packetSize), WS_DISCOVERY_BUFFER_SIZE);

        const int bytesRead = discoveryUdp.read(reinterpret_cast<uint8_t*>(discoveryBuffer), bytesToRead);

        if (bytesRead <= 0) {
            PersistentLog::record(PersistentLog::Event::DiscoveryResponse, PersistentLog::Outcome::Failure, "read_failed", remoteIp, remotePort, bytesRead, millis() - startedAtMs, static_cast<uint32_t>(packetSize), logFlags, PersistentLog::Level::Error, false, logContextId);
            continue;
        }

        discoveryBuffer[bytesRead] = '\0';
        addDiscoveryResponse(String(discoveryBuffer));
        const size_t addedCandidates = discoveredCount - candidateCountBefore;
        PersistentLog::record(PersistentLog::Event::DiscoveryResponse, PersistentLog::Outcome::Success, responseTruncated ? "response_truncated" : addedCandidates > 0 ? "candidate_parsed" : "response_received", remoteIp, remotePort, static_cast<int>(addedCandidates), millis() - startedAtMs, static_cast<uint32_t>(bytesRead), logFlags, responseTruncated ? PersistentLog::Level::Warning : PersistentLog::Level::Info, false, logContextId);
    }

    discoveryUdp.stop();

    lastDiscoveryResult = currentTimestamp() + " - candidatos=" + String(discoveryIdentityQueueCount) + "; cadastradas=" + String(cameraCount) + "; identificacao pendente";
    PersistentLog::record(PersistentLog::Event::DiscoveryResult, discoveryIdentityQueueCount > 0 ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, discoveryIdentityQueueCount > 0 ? "identity_pending" : "no_camera_found", IPAddress(), 0, 0, millis() - startedAtMs, discoveryIdentityQueueCount, logFlags, discoveryIdentityQueueCount > 0 ? PersistentLog::Level::Info : PersistentLog::Level::Warning, true, logContextId);
    return discoveryIdentityQueueCount > 0;
}

ServiceProbe probeOnvif(CameraContext& ctx, const CameraTarget& target, uint16_t contextId)
{
    ServiceProbe result;
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();

    if (!target.valid || target.onvifUrl.isEmpty()) {
        return result;
    }

    ParsedUrl parsed;

    if (!parseHttpUrl(target.onvifUrl, parsed) || parsed.secure) {
        return result;
    }

    WiFiClient client;
    const unsigned long startedAt = millis();
    const TcpConnectionAttempt connection = connectTcp(client, parsed.ip, parsed.port, ctx.settings.tcpConnectTimeoutMs);

    if (!connection.connected) {
        client.stop();
        result.latencyMs = connection.durationMs;
        PersistentLog::record(PersistentLog::Event::OnvifProbe, PersistentLog::Outcome::Failure, connection.detail, parsed.ip, parsed.port, connection.errorCode, result.latencyMs, ctx.settings.tcpConnectTimeoutMs, PersistentLog::FlagOutbound, PersistentLog::Level::Error, false, logContextId, ctx.id);
        return result;
    }

    result.tcpConnected = true;
    result.latencyMs = millis() - startedAt;

    String hostHeader = parsed.ip.toString();

    if (parsed.port != 80) {
        hostHeader += ':';
        hostHeader += parsed.port;
    }

    client.print("POST ");
    client.print(parsed.path);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(hostHeader);
    client.print("\r\nContent-Type: application/soap+xml; charset=utf-8; action=\"");
    client.print("http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime");
    client.print("\"\r\nConnection: close\r\nContent-Length: ");
    client.print(strlen(SOAP_GET_SYSTEM_TIME));
    client.print("\r\n\r\n");
    client.print(SOAP_GET_SYSTEM_TIME);

    const unsigned long responseStartedAt = millis();
    String responseHeader;
    responseHeader.reserve(512);

    while (millis() - responseStartedAt < ctx.settings.serviceResponseTimeoutMs) {
        updateCameraAlertLed();

        while (client.available()) {
            const char value = static_cast<char>(client.read());

            if (responseHeader.length() < 1024) {
                responseHeader += value;
            }
        }

        if (responseHeader.indexOf("\r\n\r\n") >= 0) {
            break;
        }

        if (!client.connected() && !client.available()) {
            break;
        }

        delay(1);
    }

    client.stop();
    result.latencyMs = millis() - startedAt;

    const int firstLineEnd = responseHeader.indexOf("\r\n");
    const String firstLine = firstLineEnd >= 0 ? responseHeader.substring(0, firstLineEnd) : responseHeader;

    if (firstLine.startsWith("HTTP/")) {
        const int firstSpace = firstLine.indexOf(' ');

        if (firstSpace >= 0) {
            result.statusCode = firstLine.substring(firstSpace + 1).toInt();
        }
    }

    String normalizedHeader = responseHeader;
    normalizedHeader.toLowerCase();

    result.soapResponse = normalizedHeader.indexOf("content-type: application/soap+xml") >= 0 || normalizedHeader.indexOf("content-type: text/xml") >= 0;
    result.protocolResponded = result.statusCode == 401 || result.statusCode == 403 || result.soapResponse;

    const char* resultDetail = result.protocolResponded ? "GetSystemDateAndTime" : result.statusCode != 0 ? "non_soap_response" : "invalid_response";

    PersistentLog::record(PersistentLog::Event::OnvifProbe, result.protocolResponded ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, resultDetail, parsed.ip, parsed.port, result.statusCode, result.latencyMs, ctx.settings.tcpConnectTimeoutMs, PersistentLog::FlagOutbound, result.protocolResponded ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, logContextId, ctx.id);

    return result;
}

bool resolveCameraOnvifEndpoint(CameraContext& ctx, uint16_t contextId)
{
    if (!ctx.camera.valid) {
        return false;
    }

    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    ServiceProbe currentProbe = probeOnvif(ctx, ctx.camera, logContextId);

    if (currentProbe.protocolResponded) {
        ctx.lastOnvifProbe = currentProbe;
        ctx.preferredFailureStreak = 0;
        return true;
    }

    for (size_t portIndex = 0; portIndex < ONVIF_PORT_CANDIDATE_COUNT; portIndex++) {
        const uint16_t candidatePort = ONVIF_PORT_CANDIDATES[portIndex];

        if (candidatePort == ctx.camera.onvifPort && ctx.camera.onvifPath == DEFAULT_ONVIF_PATH) {
            continue;
        }

        CameraTarget candidate = ctx.camera;
        candidate.onvifPort = candidatePort;
        candidate.onvifPath = DEFAULT_ONVIF_PATH;
        candidate.onvifUrl = makeOnvifUrl(candidate.ip, candidate.onvifPort, candidate.onvifPath);

        const ServiceProbe candidateProbe = probeOnvif(ctx, candidate, logContextId);

        if (!candidateProbe.protocolResponded) {
            continue;
        }
        CameraTarget observed = candidate;
        observed.endpointUuid = String();
        observed.serialNumber = String();
        observed.manufacturer = String();
        observed.model = String();
        observed.interfaceMacs = String();
        std::unique_ptr<CameraContext> identityProbe(new (std::nothrow) CameraContext());
        if (!identityProbe)
            return false;
        identityProbe->camera = observed;
        if (!probeCameraIdentity(*identityProbe, observed, logContextId) || !cameraIdentityMatches(ctx.camera, observed))
            continue;
        const CameraTarget previous = ctx.camera;
        const uint8_t previousAlternateCount = ctx.alternateOnvifCount;
        bool oldEndpointKnown = false;
        for (uint8_t index = 0; index < previousAlternateCount; index++)
            if (ctx.alternateOnvifUrls[index] == previous.onvifUrl)
                oldEndpointKnown = true;
        if (!oldEndpointKnown && ctx.alternateOnvifCount < CAMERA_ALTERNATE_ENDPOINT_CAPACITY)
            ctx.alternateOnvifUrls[ctx.alternateOnvifCount++] = previous.onvifUrl;
        ctx.camera = observed;
        ctx.camera.rtspPort = ctx.settings.cameraRtspPort;
        if (!persistCamera(ctx)) {
            ctx.camera = previous;
            if (ctx.alternateOnvifCount > previousAlternateCount)
                ctx.alternateOnvifUrls[--ctx.alternateOnvifCount] = String();
            continue;
        }
        ctx.lastOnvifProbe = candidateProbe;
        invalidateSnapshotTarget(ctx);
        invalidateOnvifServiceCatalog(ctx);
        resetSnapshotProfileFailover(ctx);
        ctx.lastSnapshotResolveAttemptAtMs = 0;
        registryRevision++;

        Serial.printf("Servico ONVIF confirmado na porta %u.\n", ctx.camera.onvifPort);
        return true;
    }

    ctx.lastOnvifProbe = currentProbe;
    if (ctx.preferredFailureStreak < 255)
        ctx.preferredFailureStreak++;
    if (ctx.preferredFailureStreak >= 2 && switchToAlternateEndpoint(ctx))
        return true;
    return false;
}

bool switchToAlternateEndpoint(CameraContext& ctx)
{
    if (ctx.alternateOnvifCount == 0 || ESP.getFreeHeap() < sizeof(CameraContext) + MAX_ONVIF_RESPONSE_SIZE + CAMERA_OPERATION_HEAP_RESERVE || ESP.getMaxAllocHeap() < MAX_ONVIF_RESPONSE_SIZE + 4096 || (ctx.lastAlternateAttemptAtMs != 0 && millis() - ctx.lastAlternateAttemptAtMs < 30000UL))
        return false;
    ctx.lastAlternateAttemptAtMs = millis();
    const uint8_t index = ctx.nextAlternateIndex % ctx.alternateOnvifCount;
    ctx.nextAlternateIndex = (index + 1) % ctx.alternateOnvifCount;
    ParsedUrl parsed;
    if (!parseHttpUrl(ctx.alternateOnvifUrls[index], parsed) || parsed.secure || parsed.ip == WiFi.localIP() || !CameraPolicy::safeHttpPath(parsed.path.c_str(), 160, false))
        return false;
    CameraTarget target = ctx.camera;
    target.ip = parsed.ip;
    target.onvifPort = parsed.port;
    target.onvifPath = parsed.path;
    target.onvifUrl = ctx.alternateOnvifUrls[index];
    target.endpointUuid = String();
    target.serialNumber = String();
    target.manufacturer = String();
    target.model = String();
    target.interfaceMacs = String();
    std::unique_ptr<CameraContext> probe(new (std::nothrow) CameraContext());
    if (!probe)
        return false;
    probe->camera = target;
    if (!probeCameraIdentity(*probe, target))
        return false;
    if (!cameraIdentityMatches(ctx.camera, target)) {
        const CameraPolicy::Identity known{ctx.camera.endpointUuid.c_str(), ctx.camera.serialNumber.c_str(), ctx.camera.manufacturer.c_str(), ctx.camera.model.c_str(), ctx.camera.interfaceMacs.c_str()};
        const CameraPolicy::Identity seen{target.endpointUuid.c_str(), target.serialNumber.c_str(), target.manufacturer.c_str(), target.model.c_str(), target.interfaceMacs.c_str()};
        if (CameraPolicy::identifiersConflict(known, seen)) {
            const String previousAlternative = ctx.alternateOnvifUrls[index];
            for (uint8_t remaining = index + 1; remaining < ctx.alternateOnvifCount; remaining++)
                ctx.alternateOnvifUrls[remaining - 1] = ctx.alternateOnvifUrls[remaining];
            ctx.alternateOnvifUrls[--ctx.alternateOnvifCount] = String();
            if (!persistCamera(ctx)) {
                for (uint8_t remaining = ctx.alternateOnvifCount; remaining > index; remaining--)
                    ctx.alternateOnvifUrls[remaining] = ctx.alternateOnvifUrls[remaining - 1];
                ctx.alternateOnvifUrls[index] = previousAlternative;
                ctx.alternateOnvifCount++;
            } else
                registryRevision++;
        }
        return false;
    }
    if (!CameraPolicy::allowEndpointPromotion(ctx.preferredFailureStreak, true, true))
        return false;
    const CameraTarget previous = ctx.camera;
    const String alternative = ctx.alternateOnvifUrls[index];
    ctx.camera = target;
    ctx.camera.rtspPort = ctx.settings.cameraRtspPort;
    ctx.alternateOnvifUrls[index] = previous.onvifUrl;
    if (!persistCamera(ctx)) {
        ctx.camera = previous;
        ctx.alternateOnvifUrls[index] = alternative;
        return false;
    }
    invalidateSnapshotTarget(ctx);
    invalidateOnvifServiceCatalog(ctx);
    resetCameraSnapshotHttpSession(ctx);
    resetCameraCheckSequences(ctx, "Endereco alternativo validado; aguardando verificacao");
    ctx.previousCameraIp = previous.ip.toString();
    ctx.pendingIpChangeEmail = previous.ip != target.ip;
    ctx.preferredFailureStreak = 0;
    ctx.nextAlternateIndex = 0;
    registryRevision++;
    return true;
}

bool postOnvifSoap(CameraContext& ctx, const String& serviceUrl, const char* action, const String& body, String& responseBody, bool requireResponseBody, int* finalStatusCode, bool* transportFailure, uint32_t timeoutMs, uint16_t contextId)
{
    if (timeoutMs == 0)
        timeoutMs = ctx.settings.serviceResponseTimeoutMs;
    responseBody = String();
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();

    if (transportFailure != nullptr) {
        *transportFailure = false;
    }

    const String wsseHeader = makeWsseSecurityHeader(ctx);
    String envelope;
    envelope.reserve(body.length() + wsseHeader.length() + 420);
    envelope += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    envelope += "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" ";
    envelope += "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" ";
    envelope += "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" ";
    envelope += "xmlns:tr2=\"http://www.onvif.org/ver20/media/wsdl\" ";
    envelope += "xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" ";
    envelope += "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" ";
    envelope += "xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\" ";
    envelope += "xmlns:tan=\"http://www.onvif.org/ver20/analytics/wsdl\" ";
    envelope += "xmlns:tmd=\"http://www.onvif.org/ver10/deviceIO/wsdl\" ";
    envelope += "xmlns:tt=\"http://www.onvif.org/ver10/schema\">";
    envelope += wsseHeader;
    envelope += "<s:Body>";
    envelope += body;
    envelope += "</s:Body></s:Envelope>";

    WiFiClient client;
    HTTPClient request;
    const char* responseHeaders[] = {"WWW-Authenticate"};
    request.setConnectTimeout(min(ctx.settings.tcpConnectTimeoutMs, timeoutMs));
    request.setTimeout(timeoutMs);
    request.setReuse(false);
    request.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    request.useHTTP10(true);
    request.setUserAgent(String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION);
    request.collectHeaders(responseHeaders, 1);

    String contentType = "application/soap+xml; charset=utf-8; action=\"";
    contentType += action;
    contentType += '\"';

    ParsedUrl logEndpoint;
    parseHttpUrl(serviceUrl, logEndpoint);
    const char* logAction = strrchr(action, '/');
    logAction = logAction == nullptr ? action : logAction + 1;

    const auto executeAttempt = [&](const String& digestAuthorization, bool authenticationRetry) {
        const unsigned long attemptStartedAtMs = millis();

        if (!request.begin(client, serviceUrl)) {
            PersistentLog::record(PersistentLog::Event::OnvifSoap, PersistentLog::Outcome::Failure, logAction, logEndpoint.ip, logEndpoint.port, -1, millis() - attemptStartedAtMs, static_cast<uint32_t>(envelope.length()), static_cast<uint8_t>(PersistentLog::FlagOutbound | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), PersistentLog::Level::Automatic, false, logContextId, ctx.id);
            return -1;
        }

        request.addHeader("Content-Type", contentType);
        applyCameraAuthorization(ctx, request, digestAuthorization);
        const int attemptStatusCode = request.POST(envelope);
        const bool requestAccepted = attemptStatusCode >= 200 && attemptStatusCode < 400;
        PersistentLog::record(PersistentLog::Event::OnvifSoap, requestAccepted ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, logAction, logEndpoint.ip, logEndpoint.port, attemptStatusCode, millis() - attemptStartedAtMs, static_cast<uint32_t>(envelope.length()), static_cast<uint8_t>(PersistentLog::FlagOutbound | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), requestAccepted || (attemptStatusCode == 401 && !authenticationRetry && cameraCredentialsConfigured(ctx)) ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, logContextId, ctx.id);
        return attemptStatusCode;
    };

    int statusCode = executeAttempt(String(), false);

    if (statusCode == 401 && cameraCredentialsConfigured(ctx)) {
        DigestChallenge challenge;
        const String authenticateHeader = request.header("WWW-Authenticate");

        if (parseDigestChallenge(authenticateHeader, challenge)) {
            const String digestAuthorization = makeDigestAuthorization(ctx, "POST", serviceUrl, challenge);

            if (!digestAuthorization.isEmpty()) {
                request.end();
                client.stop();
                statusCode = executeAttempt(digestAuthorization, true);
            }
        }
    }

    if (finalStatusCode != nullptr) {
        *finalStatusCode = statusCode;
    }

    const int responseSize = request.getSize();
    bool responseLimitExceeded = responseSize > static_cast<int>(MAX_ONVIF_RESPONSE_SIZE);
    bool responseRead = false;
    bool responseStreamFailure = false;

    if (statusCode > 0 && !responseLimitExceeded) {
        responseRead = readHttpResponseBodyBounded(request, responseBody, MAX_ONVIF_RESPONSE_SIZE, responseLimitExceeded, &responseStreamFailure);
    }

    if (transportFailure != nullptr) {
        *transportFailure = statusCode < 0 || responseStreamFailure;
    }

    request.end();

    int faultStart = 0;
    int faultEnd = 0;
    const bool soapFault = !responseBody.isEmpty() && findNextOpeningTag(responseBody, "Fault", 0, faultStart, faultEnd);

    if (statusCode < 200 || statusCode >= 300 || responseLimitExceeded || soapFault) {
        return false;
    }

    return !requireResponseBody || (responseRead && !responseBody.isEmpty());
}

bool probeCameraIdentity(CameraContext& ctx, CameraTarget& target, uint16_t contextId, bool* identityConflict)
{
    if (!target.valid || target.onvifUrl.isEmpty())
        return false;
    if (identityConflict != nullptr)
        *identityConflict = false;
    String response;
    int status = 0;
    const uint32_t timeout = min(ctx.settings.serviceResponseTimeoutMs, uint32_t(1200));
    const bool deviceInfo = postOnvifSoap(ctx, target.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation", "<tds:GetDeviceInformation/>", response, true, &status, nullptr, timeout, contextId) && CameraPolicy::hasXmlLocalOpeningTag(response.c_str(), "GetDeviceInformationResponse");
    if (deviceInfo) {
        String field;
        int ignored = 0;
        if (extractLocalTag(response, "SerialNumber", 0, field, ignored))
            target.serialNumber = normalizeCameraIdentifier(limitedOnvifText(field, 128));
        if (extractLocalTag(response, "Manufacturer", 0, field, ignored))
            target.manufacturer = limitedOnvifText(field, 120);
        if (extractLocalTag(response, "Model", 0, field, ignored))
            target.model = limitedOnvifText(field, 120);
    }
    if (deviceInfo) {
        response = String();
        if (postOnvifSoap(ctx, target.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/GetNetworkInterfaces", "<tds:GetNetworkInterfaces/>", response, true, &status, nullptr, timeout, contextId) && CameraPolicy::hasXmlLocalOpeningTag(response.c_str(), "GetNetworkInterfacesResponse")) {
            int offset = 0;
            String mac;
            for (uint8_t index = 0; index < 4 && extractLocalTag(response, "HwAddress", offset, mac, offset); index++) {
                mac = limitedOnvifText(mac, 17);
                mac.toLowerCase();
                bool valid = mac.length() == 17;
                for (size_t digit = 0; valid && digit < mac.length(); digit++)
                    valid = (digit % 3 == 2) ? mac[digit] == ':' : isxdigit(static_cast<unsigned char>(mac[digit]));
                if (valid && target.interfaceMacs.indexOf(mac) < 0) {
                    if (!target.interfaceMacs.isEmpty())
                        target.interfaceMacs += ',';
                    target.interfaceMacs += mac;
                }
            }
        }
    }
    const String announcedUuid = normalizeCameraIdentifier(target.endpointUuid);
    target.endpointUuid = String();
    response = String();
    if (postOnvifSoap(ctx, target.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/GetEndpointReference", "<tds:GetEndpointReference/>", response, true, &status, nullptr, timeout, contextId) && CameraPolicy::hasXmlLocalOpeningTag(response.c_str(), "GetEndpointReferenceResponse")) {
        String guid;
        int ignored = 0;
        if (extractLocalTag(response, "GUID", 0, guid, ignored)) {
            const String verifiedUuid = normalizeCameraIdentifier(limitedOnvifText(guid, 128));
            if (!announcedUuid.isEmpty() && !verifiedUuid.isEmpty() && announcedUuid != verifiedUuid) {
                if (identityConflict != nullptr)
                    *identityConflict = true;
                return false;
            }
            target.endpointUuid = verifiedUuid;
        }
    }
    return deviceInfo || !target.endpointUuid.isEmpty();
}

void noteCameraClockFailure(CameraContext& ctx, const String& result)
{
    ctx.cameraClockStatus.checked = true;
    ctx.cameraClockStatus.utcValid = false;
    ctx.cameraClockStatus.roundTripMs = 0;
    ctx.cameraClockStatus.checks++;
    ctx.cameraClockStatus.failures++;
    ctx.cameraClockStatus.lastCheckAtMs = millis();
    ctx.cameraClockStatus.lastResult = result;
}

bool recordCameraClockResponse(CameraContext& ctx, const String& responseXml, int64_t referenceUnixTimeMs, uint32_t roundTripMs)
{
    String value;
    int ignored = 0;

    if (extractLocalTag(responseXml, "DateTimeType", 0, value, ignored)) {
        ctx.cameraClockStatus.dateTimeType = limitedOnvifText(value, 32);
        ctx.onvifMetadata.dateTimeType = ctx.cameraClockStatus.dateTimeType;
    }

    if (extractLocalTag(responseXml, "TZ", 0, value, ignored)) {
        ctx.cameraClockStatus.timeZone = limitedOnvifText(value, 48);
        ctx.onvifMetadata.timeZone = ctx.cameraClockStatus.timeZone;
    }

    if (extractLocalTag(responseXml, "DaylightSavings", 0, value, ignored)) {
        value.trim();
        ctx.cameraClockStatus.daylightSavings = value == "1" || value.equalsIgnoreCase("true");
    }

    ctx.cameraClockStatus.checked = true;
    ctx.onvifMetadata.timeLoaded = true;
    ctx.cameraClockStatus.checks++;
    ctx.cameraClockStatus.lastCheckAtMs = millis();
    ctx.cameraClockStatus.roundTripMs = roundTripMs;

    int64_t cameraUnixTimeMs = 0;

    if (referenceUnixTimeMs <= 0 || roundTripMs > CAMERA_CLOCK_MAX_ROUND_TRIP_MS || !parseOnvifUtcDateTime(responseXml, cameraUnixTimeMs)) {
        ctx.cameraClockStatus.utcValid = false;
        ctx.cameraClockStatus.failures++;
        ctx.cameraClockStatus.lastResult = roundTripMs > CAMERA_CLOCK_MAX_ROUND_TRIP_MS ? "Amostra de relogio descartada por latencia ONVIF alta" : "UTCDateTime ONVIF ausente ou invalido";
        return false;
    }

    const int64_t rawDriftMs = cameraUnixTimeMs - referenceUnixTimeMs;

    if (rawDriftMs <= INT32_MIN || rawDriftMs > INT32_MAX) {
        ctx.cameraClockStatus.utcValid = false;
        ctx.cameraClockStatus.failures++;
        ctx.cameraClockStatus.lastResult = "Desvio do relogio fora do intervalo mensuravel";
        return false;
    }

    ctx.cameraClockStatus.utcValid = true;
    ctx.cameraClockStatus.driftMs = static_cast<int32_t>(rawDriftMs);
    const int32_t absoluteDriftMs = ctx.cameraClockStatus.driftMs >= 0 ? ctx.cameraClockStatus.driftMs : -ctx.cameraClockStatus.driftMs;
    const String direction = ctx.cameraClockStatus.driftMs >= 0 ? " adiantada" : " atrasada";

    if (absoluteDriftMs <= CAMERA_CLOCK_HEALTHY_DRIFT_MS) {
        ctx.cameraClockStatus.lastResult = "Relogio da camera alinhado ao NTP do ESP32; desvio " + String(ctx.cameraClockStatus.driftMs / 1000.0F, 1) + " s";
    } else {
        ctx.cameraClockStatus.lastResult = "Camera" + direction + " em " + String(absoluteDriftMs / 1000.0F, 1) + " s frente ao NTP do ESP32";
    }

    return true;
}

void maintainCameraClock(CameraContext& ctx)
{
    if (!ctx.camera.valid || WiFi.status() != WL_CONNECTED || ctx.onvifMetadataStage != OnvifMetadataStage::Complete || !ctx.onvifServices.device.available || cameraRebootGraceActive(ctx) || healthCheckInProgress) {
        return;
    }

    const unsigned long now = millis();
    const bool clockHealthy = ctx.cameraClockStatus.utcValid && ctx.cameraClockStatus.driftMs >= -CAMERA_CLOCK_HEALTHY_DRIFT_MS && ctx.cameraClockStatus.driftMs <= CAMERA_CLOCK_HEALTHY_DRIFT_MS;
    const unsigned long intervalMs = ctx.cameraClockStatus.checked && clockHealthy ? CAMERA_CLOCK_CHECK_INTERVAL_MS : CAMERA_CLOCK_RETRY_INTERVAL_MS;

    if (ctx.cameraClockStatus.lastCheckAtMs != 0 && now - ctx.cameraClockStatus.lastCheckAtMs < intervalMs) {
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        return;
    }

    const SntpReferenceSnapshot referenceBefore = snapshotSntpReference();

    if (!sntpReferenceFresh(referenceBefore)) {
        return;
    }

    const int64_t requestStartedAtMs = synchronizedUnixTimeMs();
    const unsigned long requestStartedAtMonotonicMs = millis();
    String responseXml;
    int statusCode = 0;
    const bool succeeded = postOnvifSoap(ctx, ctx.onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime", "<tds:GetSystemDateAndTime/>", responseXml, true, &statusCode, nullptr, 1000UL);
    const int64_t requestFinishedAtMs = synchronizedUnixTimeMs();
    const SntpReferenceSnapshot referenceAfter = snapshotSntpReference();
    const uint32_t roundTripMs = millis() - requestStartedAtMonotonicMs;

    if (!succeeded || requestStartedAtMs <= 0 || requestFinishedAtMs < requestStartedAtMs) {
        noteCameraClockFailure(ctx, String("GetSystemDateAndTime falhou; HTTP ") + statusCode);
        return;
    }

    if (referenceBefore.generation != referenceAfter.generation || !sntpReferenceFresh(referenceAfter)) {
        noteCameraClockFailure(ctx, "Amostra descartada: referencia SNTP mudou durante a consulta");
        return;
    }

    const int64_t referenceUnixTimeMs = requestStartedAtMs + (requestFinishedAtMs - requestStartedAtMs) / 2LL;
    recordCameraClockResponse(ctx, responseXml, referenceUnixTimeMs, roundTripMs);
}

void invalidateOnvifServiceCatalog(CameraContext& ctx)
{
    ctx.onvifServices = OnvifServiceCatalog{};
    ctx.onvifPtz = OnvifPtzStatus{};
    ctx.onvifMetadata = OnvifDeviceMetadata{};
    ctx.cameraClockStatus = CameraClockStatus{};
    ctx.onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
    ctx.onvifMetadataStageFailureStreak = 0;
    ctx.lastOnvifMetadataRequestAtMs = 0;
    ctx.lastOnvifMetadataRequestFailed = false;
    ctx.lastBrowserSnapshotDeliveredAtMs = 0;
    ctx.lastBrowserSnapshotBytes = 0;
    ctx.lastBrowserSnapshotLatencyMs = 0;
    ctx.lastHealthEvidenceSource = HealthEvidenceSource::None;
    ctx.lastHealthEvidenceAtMs = 0;
}

String limitedOnvifText(String value, size_t maximumLength)
{
    value = xmlUnescape(value);
    value.trim();

    if (value.length() > maximumLength) {
        value.remove(maximumLength);
    }

    return value;
}

bool normalizeAdvertisedOnvifUrl(CameraContext& ctx, const String& advertisedUrl, String& normalizedUrl, bool* rewritten = nullptr)
{
    ParsedHttpResource parsed;

    if (!ctx.camera.valid || !parseHttpResourceUrl(advertisedUrl, parsed) || parsed.secure || !CameraPolicy::safeHttpPath(parsed.path.c_str(), 160, false)) {
        return false;
    }

    IPAddress advertisedIp;

    if (!advertisedIp.fromString(parsed.host.c_str())) {
        return false;
    }

    const bool unspecifiedAddress = advertisedIp[0] == 0 && advertisedIp[1] == 0 && advertisedIp[2] == 0 && advertisedIp[3] == 0;

    if (ctx.camera.ip == WiFi.localIP())
        return false;
    if (rewritten != nullptr)
        *rewritten = unspecifiedAddress || advertisedIp != ctx.camera.ip;
    normalizedUrl = makeOnvifUrl(ctx.camera.ip, parsed.port, parsed.path);
    return true;
}

bool validateRewrittenOnvifService(CameraContext& ctx, const String& serviceNamespace, const String& url, int& status)
{
    const char* prefix = nullptr;
    if (serviceNamespace == "http://www.onvif.org/ver10/media/wsdl")
        prefix = "trt";
    else if (serviceNamespace == "http://www.onvif.org/ver20/ptz/wsdl")
        prefix = "tptz";
    else
        return false;
    const String body = String("<") + prefix + ":GetServiceCapabilities/>";
    const String action = serviceNamespace + "/GetServiceCapabilities";
    String response;
    status = 0;
    return postOnvifSoap(ctx, url, action.c_str(), body, response, true, &status, nullptr, min(ctx.settings.serviceResponseTimeoutMs, uint32_t(1200))) && CameraPolicy::hasXmlLocalOpeningTag(response.c_str(), "GetServiceCapabilitiesResponse");
}

OnvifServiceEndpoint* endpointForOnvifNamespace(CameraContext& ctx, const String& serviceNamespace)
{
    if (serviceNamespace == "http://www.onvif.org/ver10/device/wsdl") {
        return &ctx.onvifServices.device;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/media/wsdl") {
        return &ctx.onvifServices.media1;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/media/wsdl") {
        return &ctx.onvifServices.media2;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/events/wsdl") {
        return &ctx.onvifServices.events;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/ptz/wsdl") {
        return &ctx.onvifServices.ptz;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/imaging/wsdl") {
        return &ctx.onvifServices.imaging;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/analytics/wsdl") {
        return &ctx.onvifServices.analytics;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/deviceIO/wsdl") {
        return &ctx.onvifServices.deviceIo;
    }

    return nullptr;
}

bool onvifCapabilityIsTrue(const String& serviceXml, const char* name)
{
    const String doubleQuoted = String(name) + "=\"true\"";
    const String singleQuoted = String(name) + "='true'";
    const String doubleQuotedOne = String(name) + "=\"1\"";
    const String singleQuotedOne = String(name) + "='1'";

    if (containsIgnoreCase(serviceXml, doubleQuoted) || containsIgnoreCase(serviceXml, singleQuoted) || serviceXml.indexOf(doubleQuotedOne) >= 0 || serviceXml.indexOf(singleQuotedOne) >= 0) {
        return true;
    }

    String value;
    int ignored = 0;

    if (!extractLocalTag(serviceXml, name, 0, value, ignored)) {
        return false;
    }

    value.trim();
    return value == "1" || containsIgnoreCase(value, "true");
}

uint16_t onvifUnsignedTag(const String& xml, const char* name)
{
    String value;
    int ignored = 0;

    if (!extractLocalTag(xml, name, 0, value, ignored)) {
        return 0;
    }

    const long parsed = value.toInt();
    return parsed > 0 && parsed <= 65535 ? static_cast<uint16_t>(parsed) : 0;
}

void collectOnvifServiceCapabilities(CameraContext& ctx, const String& serviceNamespace, const String& serviceXml)
{
    if (serviceNamespace == "http://www.onvif.org/ver10/device/wsdl") {
        ctx.onvifServices.usernameToken = onvifCapabilityIsTrue(serviceXml, "UsernameToken");
        ctx.onvifServices.httpDigest = onvifCapabilityIsTrue(serviceXml, "HttpDigest");
    } else if (serviceNamespace == "http://www.onvif.org/ver10/media/wsdl") {
        ctx.onvifServices.mediaSnapshotUri = onvifCapabilityIsTrue(serviceXml, "SnapshotUri");
    } else if (serviceNamespace == "http://www.onvif.org/ver10/events/wsdl") {
        ctx.onvifServices.eventsPullPoint = onvifCapabilityIsTrue(serviceXml, "WSPullPointSupport");
    } else if (serviceNamespace == "http://www.onvif.org/ver20/imaging/wsdl") {
        ctx.onvifServices.imagingStabilizationAdvertised = onvifCapabilityIsTrue(serviceXml, "ImageStabilization");
    } else if (serviceNamespace == "http://www.onvif.org/ver20/analytics/wsdl") {
        ctx.onvifServices.analyticsRules = onvifCapabilityIsTrue(serviceXml, "RuleSupport");
        ctx.onvifServices.analyticsModules = onvifCapabilityIsTrue(serviceXml, "AnalyticsModuleSupport");
    }
}

bool parseOnvifServices(CameraContext& ctx, const String& servicesXml)
{
    if (!CameraPolicy::hasXmlLocalOpeningTag(servicesXml.c_str(), "GetServicesResponse"))
        return false;
    int serviceStart = 0;
    int serviceOpenEnd = 0;
    int searchPosition = 0;

    while (findNextOpeningTag(servicesXml, "Service", searchPosition, serviceStart, serviceOpenEnd)) {
        int nextServiceStart = 0;
        int nextServiceOpenEnd = 0;
        const bool hasNextService = findNextOpeningTag(servicesXml, "Service", serviceOpenEnd + 1, nextServiceStart, nextServiceOpenEnd);
        const int serviceEnd = hasNextService ? nextServiceStart : servicesXml.length();
        const String serviceXml = servicesXml.substring(serviceStart, serviceEnd);
        String serviceNamespace;
        String advertisedUrl;
        int ignored = 0;

        if (extractLocalTag(serviceXml, "Namespace", 0, serviceNamespace, ignored) && extractLocalTag(serviceXml, "XAddr", 0, advertisedUrl, ignored)) {
            serviceNamespace = limitedOnvifText(serviceNamespace, 96);
            advertisedUrl = limitedOnvifText(advertisedUrl, 512);
            OnvifServiceEndpoint* endpoint = endpointForOnvifNamespace(ctx, serviceNamespace);

            if (endpoint != nullptr && !endpoint->available) {
                String normalizedUrl;
                bool rewritten = false;
                const bool normalized = normalizeAdvertisedOnvifUrl(ctx, advertisedUrl, normalizedUrl, &rewritten);
                int validationStatus = 0;
                const bool validated = normalized && (!rewritten || validateRewrittenOnvifService(ctx, serviceNamespace, normalizedUrl, validationStatus));
                ParsedUrl advertised;
                ParsedUrl effective;
                parseHttpUrl(advertisedUrl, advertised);
                parseHttpUrl(normalizedUrl, effective);
                const char* reason = validated ? (rewritten ? "rewritten_validated" : "advertised") : !normalized ? "invalid_destination" : validationStatus == 401 || validationStatus == 403 ? "authentication" : validationStatus >= 200 && validationStatus < 300 ? "invalid_response" : validationStatus == 0 ? "unvalidated" : "inaccessible";
                Serial.printf("ONVIF service %s advertised=%s:%u effective=%s:%u status=%d %s\n", serviceNamespace.c_str(), advertised.ip.toString().c_str(), advertised.port, effective.ip.toString().c_str(), effective.port, validationStatus, reason);
                if (validated) {
                    endpoint->available = true;
                    endpoint->validated = rewritten;
                    endpoint->url = normalizedUrl;
                    endpoint->majorVersion = onvifUnsignedTag(serviceXml, "Major");
                    endpoint->minorVersion = onvifUnsignedTag(serviceXml, "Minor");
                    ctx.onvifServices.serviceCount++;
                    collectOnvifServiceCapabilities(ctx, serviceNamespace, serviceXml);
                } else {
                    ctx.onvifServices.rejectedEndpoints++;
                    if (validationStatus == 401 || validationStatus == 403)
                        ctx.onvifServices.rejectedAuthenticationEndpoints++;
                    else if (validationStatus >= 200 && validationStatus < 300)
                        ctx.onvifServices.invalidResponseEndpoints++;
                    else if (normalized && rewritten && validationStatus != 0)
                        ctx.onvifServices.inaccessibleEndpoints++;
                    else if (normalized && rewritten)
                        ctx.onvifServices.unvalidatedEndpoints++;
                }
            }
        }

        if (!hasNextService) {
            break;
        }

        searchPosition = nextServiceStart;
    }

    return true;
}

bool resolveOnvifServiceCatalog(CameraContext& ctx, bool forceRefresh, uint16_t contextId)
{
    if (ctx.onvifServices.resolved && !forceRefresh) {
        const bool fallbackRetryDue = (ctx.onvifServices.usedMediaFallback || ctx.onvifServices.stale) && millis() - ctx.onvifServices.resolvedAtMs >= ONVIF_CATALOG_FALLBACK_RETRY_MS;

        if (!fallbackRetryDue) {
            return ctx.onvifServices.media1.available;
        }
    }

    const uint32_t previousAttempts = ctx.onvifServices.attempts;
    const uint32_t previousSuccesses = ctx.onvifServices.successes;
    const uint32_t previousFailures = ctx.onvifServices.failures;
    const bool previousUsedMediaFallback = ctx.onvifServices.usedMediaFallback;
    const OnvifServiceCatalog previousCatalog = ctx.onvifServices;
    ctx.onvifServices = OnvifServiceCatalog{};
    ctx.onvifServices.attempts = previousAttempts + 1;
    ctx.onvifServices.successes = previousSuccesses;
    ctx.onvifServices.failures = previousFailures;
    const unsigned long startedAtMs = millis();
    String responseXml;
    int statusCode = 0;
    bool transportFailure = false;

    const bool received = postOnvifSoap(ctx, ctx.camera.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/GetServices", "<tds:GetServices><tds:IncludeCapability>true</tds:IncludeCapability></tds:GetServices>", responseXml, true, &statusCode, &transportFailure, ctx.settings.serviceResponseTimeoutMs, contextId);

    ctx.onvifServices.lastLatencyMs = millis() - startedAtMs;
    ctx.onvifServices.lastStatusCode = statusCode;

    const bool catalogParsed = received && parseOnvifServices(ctx, responseXml);
    ctx.onvifServices.catalogValid = catalogParsed;
    if (catalogParsed) {
        ctx.onvifServices.successes++;
        ctx.onvifServices.lastResult = String("GetServices retornou ") + String(ctx.onvifServices.serviceCount) + " servicos validados; rejeitados=" + String(ctx.onvifServices.rejectedEndpoints);
    } else {
        ctx.onvifServices.failures++;
        ctx.onvifServices.lastResult = transportFailure ? "GetServices falhou no transporte" : statusCode == 401 || statusCode == 403 ? "GetServices requer outra autorizacao" : responseXml.indexOf("Fault") >= 0 ? "GetServices retornou SOAP Fault" : statusCode >= 400 ? String("GetServices HTTP ") + statusCode : "GetServices retornou resposta invalida";
    }
    if ((!catalogParsed || (ctx.onvifServices.serviceCount == 0 && ctx.onvifServices.rejectedEndpoints > 0)) && previousCatalog.catalogValid && previousCatalog.serviceCount > 0) {
        const String failure = ctx.onvifServices.lastResult;
        const uint32_t attempts = ctx.onvifServices.attempts;
        const uint32_t successes = ctx.onvifServices.successes;
        const uint32_t failures = ctx.onvifServices.failures;
        const int lastStatus = ctx.onvifServices.lastStatusCode;
        ctx.onvifServices = previousCatalog;
        ctx.onvifServices.attempts = attempts;
        ctx.onvifServices.successes = successes;
        ctx.onvifServices.failures = failures;
        ctx.onvifServices.lastStatusCode = lastStatus;
        ctx.onvifServices.lastResult = failure + "; catalogo anterior desatualizado";
        ctx.onvifServices.stale = true;
        ctx.onvifServices.resolvedAtMs = millis();
        return ctx.onvifServices.media1.available;
    }

    if (!ctx.onvifServices.device.available) {
        ctx.onvifServices.device.available = true;
        ctx.onvifServices.device.validated = catalogParsed;
        ctx.onvifServices.device.url = ctx.camera.onvifUrl;
        ctx.onvifServices.device.majorVersion = 1;
    }

    if (!ctx.onvifServices.media1.available) {
        ctx.onvifServices.media1.available = true;
        ctx.onvifServices.media1.url = makeOnvifUrl(ctx.camera.ip, ctx.camera.onvifPort, DEFAULT_ONVIF_MEDIA_PATH);
        ctx.onvifServices.media1.majorVersion = 1;
        ctx.onvifServices.usedMediaFallback = true;
    }

    ctx.onvifServices.resolved = true;
    ctx.onvifServices.resolvedAtMs = millis();

    if (ctx.onvifServices.ptz.available && (!previousCatalog.ptz.available || previousCatalog.ptz.url != ctx.onvifServices.ptz.url))
        ctx.onvifPtz = OnvifPtzStatus{};

    if (previousUsedMediaFallback && !ctx.onvifServices.usedMediaFallback) {
        ctx.onvifMetadata = OnvifDeviceMetadata{};
        ctx.onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
        ctx.onvifMetadataStageFailureStreak = 0;
        ctx.lastOnvifMetadataRequestAtMs = 0;
        ctx.lastOnvifMetadataRequestFailed = false;
    }

    return ctx.onvifServices.media1.available;
}

uint16_t countOnvifOpeningTags(const String& xml, const char* localName)
{
    uint16_t count = 0;
    int start = 0;
    int end = 0;
    int position = 0;

    while (findNextOpeningTag(xml, localName, position, start, end)) {
        if (count < UINT16_MAX) {
            count++;
        }

        position = end + 1;
    }

    return count;
}

uint16_t countOnvifTopicMarkers(const String& xml)
{
    uint16_t count = 0;
    const char* markers[] = {"topic=\"true\"", "topic='true'"};

    for (const char* marker : markers) {
        int position = 0;
        const size_t markerLength = strlen(marker);

        while ((position = xml.indexOf(marker, position)) >= 0) {
            if (count < UINT16_MAX) {
                count++;
            }

            position += markerLength;
        }
    }

    return count;
}

void completeOnvifMetadataRefresh(CameraContext& ctx)
{
    ctx.onvifMetadataStage = OnvifMetadataStage::Complete;
    ctx.lastIdentityVerificationAtMs = millis();
    ctx.onvifMetadata.loadedAtMs = millis();
    ctx.onvifMetadataStageFailureStreak = 0;
    ctx.lastOnvifMetadataRequestFailed = false;
    ctx.onvifMetadata.lastResult = String(ctx.onvifMetadata.requests - ctx.onvifMetadata.failures) + "/" + String(ctx.onvifMetadata.requests) + " consultas ONVIF de leitura concluidas";
}

void advanceOnvifMetadataStage(CameraContext& ctx)
{
    switch (ctx.onvifMetadataStage) {
        case OnvifMetadataStage::DeviceInformation:
            ctx.onvifMetadataStage = OnvifMetadataStage::IdentityReference;
            break;
        case OnvifMetadataStage::IdentityReference:
            ctx.onvifMetadataStage = OnvifMetadataStage::NetworkInterfaces;
            break;
        case OnvifMetadataStage::NetworkInterfaces:
            ctx.onvifMetadataStage = OnvifMetadataStage::Scopes;
            break;
        case OnvifMetadataStage::Scopes:
            ctx.onvifMetadataStage = OnvifMetadataStage::SystemTime;
            break;
        case OnvifMetadataStage::SystemTime:
            ctx.onvifMetadataStage = OnvifMetadataStage::EventProperties;
            break;
        case OnvifMetadataStage::EventProperties:
            completeOnvifMetadataRefresh(ctx);
            break;
        case OnvifMetadataStage::Complete:
            break;
    }
}

void noteOnvifMetadataFailure(CameraContext& ctx, const char* operation, int statusCode)
{
    ctx.onvifMetadata.failures++;

    if (ctx.onvifMetadataStageFailureStreak < 255) {
        ctx.onvifMetadataStageFailureStreak++;
    }

    const bool permanentStatus = (statusCode >= 400 && statusCode <= 499 && statusCode != 408 && statusCode != 429) || statusCode == 501;
    const bool retryLimitReached = ctx.onvifMetadataStageFailureStreak >= 3;

    if (permanentStatus || retryLimitReached) {
        ctx.onvifMetadata.lastResult = String(operation) + " nao suportado ou indisponivel; etapa ignorada";
        ctx.onvifMetadataStageFailureStreak = 0;
        ctx.lastOnvifMetadataRequestFailed = false;
        advanceOnvifMetadataStage(ctx);
        return;
    }

    ctx.lastOnvifMetadataRequestFailed = true;
    ctx.onvifMetadata.lastResult = String(operation) + " falhou; HTTP " + String(statusCode) + "; nova tentativa em 30 s";
}

void suspendCameraOnIdentityConflict(CameraContext& ctx, const char* reason)
{
    suspendCameraForIdentityConflict(ctx, reason);
}

void refreshOnvifDeviceMetadata(CameraContext& ctx)
{
    if (!ctx.camera.valid || WiFi.status() != WL_CONNECTED) {
        return;
    }

    const unsigned long now = millis();

    if (ctx.onvifMetadataStage == OnvifMetadataStage::Complete) {
        if (ctx.lastIdentityVerificationAtMs == 0 || now - ctx.lastIdentityVerificationAtMs >= 15UL * 60UL * 1000UL) {
            ctx.onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
            ctx.onvifMetadataStageFailureStreak = 0;
            return;
        }
        const bool fallbackRetryDue = (ctx.onvifServices.usedMediaFallback || ctx.onvifServices.stale) && now - ctx.onvifServices.resolvedAtMs >= ONVIF_CATALOG_FALLBACK_RETRY_MS;

        if (!fallbackRetryDue) {
            return;
        }

        CameraIoGuard cameraIoGuard;

        if (cameraIoGuard.acquired()) {
            resolveOnvifServiceCatalog(ctx);
        }

        return;
    }

    const unsigned long retryIntervalMs = ctx.lastOnvifMetadataRequestFailed ? 30000UL : 250UL;

    if (ctx.lastOnvifMetadataRequestAtMs != 0 && now - ctx.lastOnvifMetadataRequestAtMs < retryIntervalMs) {
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        return;
    }

    if (!resolveOnvifServiceCatalog(ctx)) {
        ctx.lastOnvifMetadataRequestAtMs = now;
        ctx.lastOnvifMetadataRequestFailed = true;
        ctx.onvifMetadata.lastResult = "Catalogo ONVIF indisponivel; nova tentativa em 30 s";
        return;
    }

    String responseXml;
    int statusCode = 0;
    bool succeeded = false;
    ctx.onvifMetadata.requests++;
    ctx.lastOnvifMetadataRequestAtMs = millis();

    switch (ctx.onvifMetadataStage) {
        case OnvifMetadataStage::DeviceInformation: {
            const CameraTarget previousIdentity = ctx.camera;
            succeeded = postOnvifSoap(ctx, ctx.onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation", "<tds:GetDeviceInformation/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure(ctx, "GetDeviceInformation", statusCode);
                return;
            }

            String value;
            int ignored = 0;

            if (extractLocalTag(responseXml, "Manufacturer", 0, value, ignored)) {
                ctx.onvifMetadata.manufacturer = limitedOnvifText(value);
            }

            if (extractLocalTag(responseXml, "Model", 0, value, ignored)) {
                ctx.onvifMetadata.model = limitedOnvifText(value);
            }

            if (extractLocalTag(responseXml, "FirmwareVersion", 0, value, ignored)) {
                ctx.onvifMetadata.firmware = limitedOnvifText(value);
            }

            if ((!ctx.camera.manufacturer.isEmpty() && !ctx.onvifMetadata.manufacturer.isEmpty() && !ctx.camera.manufacturer.equalsIgnoreCase(ctx.onvifMetadata.manufacturer)) || (!ctx.camera.model.isEmpty() && !ctx.onvifMetadata.model.isEmpty() && !ctx.camera.model.equalsIgnoreCase(ctx.onvifMetadata.model))) {
                suspendCameraOnIdentityConflict(ctx, "Fabricante ou modelo mudou neste endereco; acesso suspenso");
                return;
            }

            bool identityChanged = false;
            if (extractLocalTag(responseXml, "SerialNumber", 0, value, ignored)) {
                const String serial = normalizeCameraIdentifier(limitedOnvifText(value, 128));
                if (!serial.isEmpty() && !ctx.camera.serialNumber.isEmpty() && ctx.camera.serialNumber != serial) {
                    suspendCameraOnIdentityConflict(ctx, "Numero de serie mudou neste endereco; acesso suspenso");
                    return;
                }
                if (ctx.camera.serialNumber != serial && !serial.isEmpty()) {
                    ctx.camera.serialNumber = serial;
                    identityChanged = true;
                }
            }
            if (ctx.camera.manufacturer.isEmpty() && !ctx.onvifMetadata.manufacturer.isEmpty()) {
                ctx.camera.manufacturer = ctx.onvifMetadata.manufacturer;
                identityChanged = true;
            }
            if (ctx.camera.model.isEmpty() && !ctx.onvifMetadata.model.isEmpty()) {
                ctx.camera.model = ctx.onvifMetadata.model;
                identityChanged = true;
            }
            if (identityChanged) {
                if (persistCamera(ctx))
                    registryRevision++;
                else {
                    ctx.camera = previousIdentity;
                    ctx.onvifMetadata.lastResult = "NVS sem espaco para identidade; nova tentativa apos reinicio";
                }
            }
            ctx.onvifMetadata.loaded = true;
            ctx.onvifMetadataStage = OnvifMetadataStage::IdentityReference;
            break;
        }

        case OnvifMetadataStage::IdentityReference: {
            const CameraTarget previousIdentity = ctx.camera;
            succeeded = postOnvifSoap(ctx, ctx.onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetEndpointReference", "<tds:GetEndpointReference/>", responseXml, true, &statusCode, nullptr, 1000UL);
            if (!succeeded) {
                noteOnvifMetadataFailure(ctx, "GetEndpointReference", statusCode);
                return;
            }
            String value;
            int ignored = 0;
            if (extractLocalTag(responseXml, "GUID", 0, value, ignored)) {
                const String uuid = normalizeCameraIdentifier(limitedOnvifText(value, 128));
                if (!uuid.isEmpty() && !ctx.camera.endpointUuid.isEmpty() && ctx.camera.endpointUuid != uuid) {
                    suspendCameraOnIdentityConflict(ctx, "EPR mudou neste endereco; acesso suspenso");
                    return;
                }
                if (!uuid.isEmpty() && ctx.camera.endpointUuid != uuid) {
                    ctx.camera.endpointUuid = uuid;
                    if (persistCamera(ctx))
                        registryRevision++;
                    else {
                        ctx.camera = previousIdentity;
                        ctx.onvifMetadata.lastResult = "NVS sem espaco para EPR";
                    }
                }
            }
            ctx.onvifMetadataStage = OnvifMetadataStage::NetworkInterfaces;
            break;
        }

        case OnvifMetadataStage::NetworkInterfaces: {
            const CameraTarget previousIdentity = ctx.camera;
            succeeded = postOnvifSoap(ctx, ctx.onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetNetworkInterfaces", "<tds:GetNetworkInterfaces/>", responseXml, true, &statusCode, nullptr, 1000UL);
            if (!succeeded) {
                noteOnvifMetadataFailure(ctx, "GetNetworkInterfaces", statusCode);
                return;
            }
            String macs;
            String mac;
            int offset = 0;
            for (uint8_t index = 0; index < 4 && extractLocalTag(responseXml, "HwAddress", offset, mac, offset); index++) {
                mac = limitedOnvifText(mac, 17);
                mac.toLowerCase();
                bool valid = mac.length() == 17;
                for (size_t digit = 0; valid && digit < mac.length(); digit++)
                    valid = digit % 3 == 2 ? mac[digit] == ':' : isxdigit(static_cast<unsigned char>(mac[digit]));
                if (valid && macs.indexOf(mac) < 0) {
                    if (!macs.isEmpty())
                        macs += ',';
                    macs += mac;
                }
            }
            if (!macs.isEmpty() && macs != ctx.camera.interfaceMacs) {
                ctx.camera.interfaceMacs = macs;
                if (persistCamera(ctx))
                    registryRevision++;
                else {
                    ctx.camera = previousIdentity;
                    ctx.onvifMetadata.lastResult = "NVS sem espaco para interfaces";
                }
            }
            ctx.onvifMetadataStage = OnvifMetadataStage::Scopes;
            break;
        }

        case OnvifMetadataStage::Scopes:
            succeeded = postOnvifSoap(ctx, ctx.onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetScopes", "<tds:GetScopes/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure(ctx, "GetScopes", statusCode);
                return;
            }

            ctx.onvifMetadata.scopeCount = countOnvifOpeningTags(responseXml, "Scopes");
            ctx.onvifMetadata.profileT = responseXml.indexOf("Profile/T") >= 0 || responseXml.indexOf("Profile%2FT") >= 0;
            ctx.onvifMetadata.scopesLoaded = true;
            ctx.onvifMetadataStage = OnvifMetadataStage::SystemTime;
            break;

        case OnvifMetadataStage::SystemTime: {
            succeeded = postOnvifSoap(ctx, ctx.onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime", "<tds:GetSystemDateAndTime/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure(ctx, "GetSystemDateAndTime", statusCode);
                return;
            }

            String value;
            int ignored = 0;

            if (extractLocalTag(responseXml, "DateTimeType", 0, value, ignored)) {
                ctx.onvifMetadata.dateTimeType = limitedOnvifText(value, 32);
            }

            if (extractLocalTag(responseXml, "TZ", 0, value, ignored)) {
                ctx.onvifMetadata.timeZone = limitedOnvifText(value, 48);
            }

            ctx.onvifMetadata.timeLoaded = true;
            ctx.onvifMetadataStage = OnvifMetadataStage::EventProperties;
            break;
        }

        case OnvifMetadataStage::EventProperties:
            if (!ctx.onvifServices.events.available) {
                ctx.onvifMetadata.requests--;
                completeOnvifMetadataRefresh(ctx);
                return;
            }

            succeeded = postOnvifSoap(ctx, ctx.onvifServices.events.url, "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesRequest", "<tev:GetEventProperties/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure(ctx, "GetEventProperties", statusCode);
                return;
            }

            ctx.onvifMetadata.eventTopicCount = countOnvifTopicMarkers(responseXml);
            ctx.onvifMetadata.eventDescriptionCount = countOnvifOpeningTags(responseXml, "MessageDescription");
            ctx.onvifMetadata.motionEvents = responseXml.indexOf("Motion") >= 0;
            ctx.onvifMetadata.tamperEvents = responseXml.indexOf("Tamper") >= 0;
            ctx.onvifMetadata.signalLossEvents = responseXml.indexOf("SignalLoss") >= 0;
            ctx.onvifMetadata.imageQualityEvents = responseXml.indexOf("ImageTooBlurry") >= 0 || responseXml.indexOf("ImageTooDark") >= 0 || responseXml.indexOf("ImageTooBright") >= 0;
            ctx.onvifMetadata.eventPropertiesLoaded = true;
            completeOnvifMetadataRefresh(ctx);
            return;

        case OnvifMetadataStage::Complete:
            return;
    }

    ctx.onvifMetadataStageFailureStreak = 0;
    ctx.lastOnvifMetadataRequestFailed = false;
    ctx.onvifMetadata.lastResult = "Inventario ONVIF em andamento";
}

struct SnapshotProfileCandidate {
    bool valid = false;
    bool ptz = false;
    String token;
    String name;
    uint32_t pixels = UINT32_MAX;
};

String normalizedSnapshotEncoding(String encoding)
{
    encoding.trim();
    encoding.toUpperCase();
    encoding.replace(".", "");
    encoding.replace("-", "");
    return encoding;
}

String snapshotProfileSection(const String& profileXml, const char* openingTag, const char* nextOpeningTag)
{
    int sectionStart = 0;
    int sectionTagEnd = 0;

    if (!findNextOpeningTag(profileXml, openingTag, 0, sectionStart, sectionTagEnd)) {
        return String();
    }

    int sectionEnd = profileXml.length();
    int ignoredEnd = 0;

    if (nextOpeningTag != nullptr) {
        findNextOpeningTag(profileXml, nextOpeningTag, sectionTagEnd + 1, sectionEnd, ignoredEnd);
    }

    return profileXml.substring(sectionStart, sectionEnd);
}

uint32_t snapshotProfileUnsignedTag(const String& xml, const char* localName)
{
    String value;
    int ignored = 0;

    if (!extractLocalTag(xml, localName, 0, value, ignored)) {
        return 0;
    }

    value.trim();
    uint32_t parsed = 0;

    for (size_t index = 0; index < value.length(); index++) {
        const char character = value.charAt(index);

        if (!isdigit(static_cast<unsigned char>(character))) {
            return 0;
        }

        parsed = parsed * 10U + static_cast<uint8_t>(character - '0');

        if (parsed > 65535U) {
            return 0;
        }
    }

    return parsed;
}

bool selectOnvifProfiles(CameraContext& ctx, const String& profilesXml, String& selectedToken, String& selectedName, String& selectedPtzToken, String& selectedPtzName)
{
    SnapshotProfileCandidate matchingJpeg;
    SnapshotProfileCandidate matching;
    SnapshotProfileCandidate jpeg;
    SnapshotProfileCandidate fallback;
    SnapshotProfileCandidate skipped;
    SnapshotProfileCandidate firstPtz;
    int searchPosition = 0;

    while (true) {
        int profileStart = 0;
        int profileTagEnd = 0;

        if (!findNextOpeningTag(profilesXml, "Profiles", searchPosition, profileStart, profileTagEnd)) {
            break;
        }

        int nextProfileStart = profilesXml.length();
        int ignoredTagEnd = 0;
        findNextOpeningTag(profilesXml, "Profiles", profileTagEnd + 1, nextProfileStart, ignoredTagEnd);
        searchPosition = profileTagEnd + 1;

        const String openingTag = profilesXml.substring(profileStart, profileTagEnd + 1);
        String token;

        if (!extractXmlAttribute(openingTag, "token", token)) {
            continue;
        }

        token = xmlUnescape(token);
        token.trim();

        if (token.isEmpty() || token.length() > 128) {
            continue;
        }

        const String profileXml = profilesXml.substring(profileTagEnd + 1, nextProfileStart);
        String name;
        int ignoredPosition = 0;

        if (!extractLocalTag(profileXml, "Name", 0, name, ignoredPosition)) {
            name = token;
        } else {
            name = xmlUnescape(name);
            name.trim();

            if (name.isEmpty()) {
                name = token;
            }
        }

        const String encoderXml = snapshotProfileSection(profileXml, "VideoEncoderConfiguration", "AudioEncoderConfiguration");
        String encoding;
        ignoredPosition = 0;
        extractLocalTag(encoderXml, "Encoding", 0, encoding, ignoredPosition);
        encoding = normalizedSnapshotEncoding(encoding);

        const uint32_t width = snapshotProfileUnsignedTag(encoderXml, "Width");
        const uint32_t height = snapshotProfileUnsignedTag(encoderXml, "Height");
        SnapshotProfileCandidate candidate;
        candidate.valid = true;
        candidate.token = token;
        candidate.name = name;
        candidate.pixels = width > 0 && height > 0 ? width * height : UINT32_MAX;
        int ptzConfigurationStart = 0;
        int ptzConfigurationEnd = 0;
        candidate.ptz = findNextOpeningTag(profileXml, "PTZConfiguration", 0, ptzConfigurationStart, ptzConfigurationEnd);

        if (candidate.ptz && !firstPtz.valid) {
            firstPtz = candidate;
        }

        if (!ctx.snapshotProfileTokenToSkip.isEmpty() && token == ctx.snapshotProfileTokenToSkip) {
            if (!skipped.valid) {
                skipped = candidate;
            }
            continue;
        }

        const bool jpegProfile = encoding == "JPEG" || encoding == "MJPEG";
        const bool nameMatches = !String(ctx.settings.snapshotProfileMatch).isEmpty() && containsIgnoreCase(name, ctx.settings.snapshotProfileMatch);

        if (nameMatches && jpegProfile && !matchingJpeg.valid) {
            matchingJpeg = candidate;
        }

        if (nameMatches && !matching.valid) {
            matching = candidate;
        }

        if (jpegProfile && !jpeg.valid) {
            jpeg = candidate;
        }

        if (!fallback.valid || candidate.pixels < fallback.pixels) {
            fallback = candidate;
        }
    }

    const SnapshotProfileCandidate* selected = matchingJpeg.valid ? &matchingJpeg : matching.valid ? &matching : jpeg.valid ? &jpeg : fallback.valid ? &fallback : &skipped;

    if (!selected->valid) {
        return false;
    }

    selectedToken = selected->token;
    selectedName = selected->name;

    const SnapshotProfileCandidate* selectedPtz = selected->ptz ? selected : firstPtz.valid ? &firstPtz : nullptr;

    if (selectedPtz != nullptr) {
        selectedPtzToken = selectedPtz->token;
        selectedPtzName = selectedPtz->name;
    } else {
        selectedPtzToken = String();
        selectedPtzName = String();
    }

    return true;
}

bool resolveSnapshotTarget(CameraContext& ctx, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    ctx.lastSnapshotResolveAttemptAtMs = millis();
    ctx.lastSnapshotResolveStatusCode = 0;
    ctx.lastSnapshotResolveTransportFailure = false;
    invalidateSnapshotTarget(ctx);

    if (!ctx.camera.valid || WiFi.status() != WL_CONNECTED) {
        ctx.lastSnapshotResult = "Camera ou Wi-Fi indisponivel";
        return false;
    }

    if (!ctx.lastOnvifProbe.protocolResponded && !resolveCameraOnvifEndpoint(ctx, logContextId)) {
        ctx.lastSnapshotResolveStatusCode = ctx.lastOnvifProbe.statusCode;
        ctx.lastSnapshotResult = "Servico ONVIF nao localizado";
        return false;
    }

    ctx.lastSnapshotResolveAttemptAtMs = millis();

    if (!resolveOnvifServiceCatalog(ctx, false, logContextId)) {
        ctx.lastSnapshotResult = "Catalogo ONVIF sem servico Media1";
        return false;
    }

    const String mediaUrl = ctx.onvifServices.media1.url;

    String profileToken;
    String profileName;
    String ptzProfileToken;
    String ptzProfileName;

    {
        String profilesXml;
        int profilesStatusCode = 0;
        bool profilesTransportFailure = false;

        if (!postOnvifSoap(ctx, mediaUrl, "http://www.onvif.org/ver10/media/wsdl/GetProfiles", "<trt:GetProfiles/>", profilesXml, true, &profilesStatusCode, &profilesTransportFailure, ctx.settings.serviceResponseTimeoutMs, logContextId)) {
            ctx.lastSnapshotResolveStatusCode = profilesStatusCode;
            ctx.lastSnapshotResolveTransportFailure = profilesTransportFailure;
            ctx.lastSnapshotResult = "ONVIF nao retornou os perfis de midia";
            return false;
        }

        ctx.lastSnapshotResolveStatusCode = profilesStatusCode;
        ctx.onvifServices.media1.validated = true;
        if (!selectOnvifProfiles(ctx, profilesXml, profileToken, profileName, ptzProfileToken, ptzProfileName)) {
            ctx.lastSnapshotResult = "Nenhum perfil ONVIF compativel";
            return false;
        }

        if (profileToken == ctx.snapshotProfileTokenToSkip) {
            resetSnapshotProfileFailover(ctx);
        }

        if (ctx.onvifServices.ptz.available && !ptzProfileToken.isEmpty()) {
            ctx.onvifPtz.profileResolved = true;
            ctx.onvifPtz.profileToken = ptzProfileToken;
            ctx.onvifPtz.profileName = ptzProfileName;
        }
    }

    String snapshotRequest;
    snapshotRequest.reserve(profileToken.length() + 96);
    snapshotRequest += "<trt:GetSnapshotUri><trt:ProfileToken>";
    snapshotRequest += htmlEscape(profileToken);
    snapshotRequest += "</trt:ProfileToken></trt:GetSnapshotUri>";

    String snapshotXml;
    int snapshotStatusCode = 0;
    bool snapshotTransportFailure = false;

    if (!postOnvifSoap(ctx, mediaUrl, "http://www.onvif.org/ver10/media/wsdl/GetSnapshotUri", snapshotRequest, snapshotXml, true, &snapshotStatusCode, &snapshotTransportFailure, ctx.settings.serviceResponseTimeoutMs, logContextId)) {
        ctx.lastSnapshotResolveStatusCode = snapshotStatusCode;
        ctx.lastSnapshotResolveTransportFailure = snapshotTransportFailure;
        ctx.lastSnapshotResult = "ONVIF nao retornou o snapshot";
        return false;
    }

    ctx.lastSnapshotResolveStatusCode = snapshotStatusCode;

    String snapshotUrl;
    int ignoredPosition = 0;

    if (!extractLocalTag(snapshotXml, "Uri", 0, snapshotUrl, ignoredPosition)) {
        ctx.lastSnapshotResult = "Resposta ONVIF sem URI de snapshot";
        return false;
    }

    snapshotUrl = xmlUnescape(snapshotUrl);
    snapshotUrl.trim();
    ParsedHttpResource parsedSnapshot;

    if (!parseHttpResourceUrl(snapshotUrl, parsedSnapshot)) {
        ctx.lastSnapshotResult = "Camera retornou um URI de snapshot invalido";
        return false;
    }

    if (parsedSnapshot.secure) {
        ctx.lastSnapshotResult = "Snapshot HTTPS local ainda nao suportado";
        return false;
    }
    if (!CameraPolicy::safeHttpPath(parsedSnapshot.path.c_str(), 512, true) || ctx.camera.ip == WiFi.localIP()) {
        ctx.lastSnapshotResult = "URI de snapshot com destino ou caminho inseguro";
        return false;
    }

    const int queryStart = parsedSnapshot.path.indexOf('?');

    if (queryStart >= 0) {
        String authenticatedPath = parsedSnapshot.path.substring(0, queryStart + 1);
        int parameterStart = queryStart + 1;

        while (parameterStart < static_cast<int>(parsedSnapshot.path.length())) {
            const int separator = parsedSnapshot.path.indexOf('&', parameterStart);
            const int parameterEnd = separator >= 0 ? separator : static_cast<int>(parsedSnapshot.path.length());
            const String parameter = parsedSnapshot.path.substring(parameterStart, parameterEnd);
            const int equals = parameter.indexOf('=');
            const String name = equals >= 0 ? parameter.substring(0, equals) : parameter;
            const char* credential = name == "user" ? ctx.settings.cameraUsername : name == "password" ? ctx.settings.cameraPassword : nullptr;

            if (credential != nullptr) {
                authenticatedPath += name + '=';

                for (const unsigned char* value = reinterpret_cast<const unsigned char*>(credential); *value != 0; value++) {
                    const bool unreserved = (*value >= 'a' && *value <= 'z') || (*value >= 'A' && *value <= 'Z') || (*value >= '0' && *value <= '9') || *value == '-' || *value == '.' || *value == '_' || *value == '~';

                    if (unreserved) {
                        authenticatedPath += static_cast<char>(*value);
                    } else {
                        char encoded[4];
                        snprintf(encoded, sizeof(encoded), "%%%02X", static_cast<unsigned>(*value));
                        authenticatedPath += encoded;
                    }
                }
            } else {
                authenticatedPath += parameter;
            }

            if (separator < 0) {
                break;
            }

            authenticatedPath += '&';
            parameterStart = separator + 1;
        }

        parsedSnapshot.path = authenticatedPath;
    }

    ctx.snapshotTarget.valid = true;
    ctx.snapshotTarget.url = makeOnvifUrl(ctx.camera.ip, parsedSnapshot.port, parsedSnapshot.path);
    ctx.snapshotTarget.profileToken = profileToken;
    ctx.snapshotTarget.profileName = profileName;
    ctx.snapshotTargetFailureStreak = 0;
    ctx.lastSnapshotResult = currentTimestamp() + " - perfil " + ctx.snapshotTarget.profileName;

    return true;
}

int onvifPtzPresetIndex(CameraContext& ctx, const String& token)
{
    for (uint16_t index = 0; index < ctx.onvifPtz.presetCount; index++) {
        if (ctx.onvifPtz.presets[index].token == token) {
            return index;
        }
    }

    return -1;
}

bool onvifPtzPresetIsDaemon(const String& token)
{
    return !String(CAMERA_DAEMON_PRESET_TOKEN).isEmpty() && token == CAMERA_DAEMON_PRESET_TOKEN;
}

String onvifPtzPresetLabel(const OnvifPtzPreset& preset)
{
    if (onvifPtzPresetIsDaemon(preset.token)) {
        return "Ponto Daemon";
    }

    if (!preset.name.isEmpty() && preset.name != preset.token) {
        return preset.name;
    }

    return String("Posição ") + preset.token;
}

bool resolveOnvifPtzProfile(CameraContext& ctx, uint16_t contextId = 0)
{
    if (ctx.onvifPtz.profileResolved && !ctx.onvifPtz.profileToken.isEmpty()) {
        return true;
    }

    if (!ctx.onvifServices.media1.available || !ctx.onvifServices.ptz.available) {
        ctx.onvifPtz.lastResult = "Servicos Media1 ou PTZ indisponiveis";
        return false;
    }

    String profilesXml;
    int statusCode = 0;

    if (!postOnvifSoap(ctx, ctx.onvifServices.media1.url, "http://www.onvif.org/ver10/media/wsdl/GetProfiles", "<trt:GetProfiles/>", profilesXml, true, &statusCode, nullptr, ctx.settings.serviceResponseTimeoutMs, contextId)) {
        ctx.onvifPtz.lastRefreshStatusCode = statusCode;
        ctx.onvifPtz.lastResult = String("GetProfiles para PTZ falhou; HTTP ") + statusCode;
        return false;
    }

    String ignoredSnapshotToken;
    String ignoredSnapshotName;
    String profileToken;
    String profileName;
    selectOnvifProfiles(ctx, profilesXml, ignoredSnapshotToken, ignoredSnapshotName, profileToken, profileName);

    if (profileToken.isEmpty()) {
        ctx.onvifPtz.lastResult = "Nenhum perfil de midia possui configuracao PTZ";
        return false;
    }

    ctx.onvifPtz.profileResolved = true;
    ctx.onvifPtz.profileToken = profileToken;
    ctx.onvifPtz.profileName = profileName;
    return true;
}

bool parseOnvifPtzPresets(const String& presetsXml, OnvifPtzPreset* parsedPresets, uint16_t& parsedCount, uint16_t& reportedCount, bool& truncated)
{
    parsedCount = 0;
    reportedCount = 0;
    truncated = false;
    int responseStart = 0;
    int responseEnd = 0;

    if (!findNextOpeningTag(presetsXml, "GetPresetsResponse", 0, responseStart, responseEnd)) {
        return false;
    }

    int searchPosition = responseEnd + 1;

    while (true) {
        int presetStart = 0;
        int presetTagEnd = 0;

        if (!findNextOpeningTag(presetsXml, "Preset", searchPosition, presetStart, presetTagEnd)) {
            break;
        }

        int nextPresetStart = presetsXml.length();
        int ignoredTagEnd = 0;
        findNextOpeningTag(presetsXml, "Preset", presetTagEnd + 1, nextPresetStart, ignoredTagEnd);
        searchPosition = presetTagEnd + 1;

        const String openingTag = presetsXml.substring(presetStart, presetTagEnd + 1);
        String token;

        if (!extractXmlAttribute(openingTag, "token", token)) {
            continue;
        }

        token = limitedOnvifText(token, 128);

        if (token.isEmpty()) {
            continue;
        }

        bool duplicate = false;

        for (uint16_t index = 0; index < parsedCount; index++) {
            if (parsedPresets[index].token == token) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            continue;
        }

        reportedCount++;

        if (parsedCount >= ONVIF_PTZ_PRESET_CAPACITY) {
            truncated = true;
            continue;
        }

        const String presetXml = presetsXml.substring(presetTagEnd + 1, nextPresetStart);
        String name;
        int ignoredPosition = 0;

        if (extractLocalTag(presetXml, "Name", 0, name, ignoredPosition)) {
            name = limitedOnvifText(name, 64);
        }

        parsedPresets[parsedCount].token = token;
        parsedPresets[parsedCount].name = name.isEmpty() ? token : name;
        parsedCount++;
    }

    return true;
}

bool refreshOnvifPtzPresets(CameraContext& ctx, uint16_t contextId = 0)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    ctx.onvifPtz.lastRefreshAttemptAtMs = millis();
    ctx.onvifPtz.refreshAttempts++;
    const unsigned long startedAtMs = millis();

    if (!resolveOnvifPtzProfile(ctx, logContextId)) {
        ctx.onvifPtz.refreshFailures++;
        ctx.onvifPtz.lastRefreshLatencyMs = millis() - startedAtMs;
        return false;
    }

    String requestBody;
    requestBody.reserve(ctx.onvifPtz.profileToken.length() + 112);
    requestBody += "<tptz:GetPresets><tptz:ProfileToken>";
    requestBody += htmlEscape(ctx.onvifPtz.profileToken);
    requestBody += "</tptz:ProfileToken></tptz:GetPresets>";

    String responseXml;
    int statusCode = 0;
    bool transportFailure = false;
    const bool received = postOnvifSoap(ctx, ctx.onvifServices.ptz.url, "http://www.onvif.org/ver20/ptz/wsdl/GetPresets", requestBody, responseXml, true, &statusCode, &transportFailure, ctx.settings.serviceResponseTimeoutMs, logContextId);
    ctx.onvifPtz.lastRefreshLatencyMs = millis() - startedAtMs;
    ctx.onvifPtz.lastRefreshStatusCode = statusCode;

    std::unique_ptr<OnvifPtzPreset[]> parsedPresets(new (std::nothrow) OnvifPtzPreset[ONVIF_PTZ_PRESET_CAPACITY]);
    uint16_t parsedCount = 0;
    uint16_t reportedCount = 0;
    bool truncated = false;

    if (!parsedPresets) {
        ctx.onvifPtz.refreshFailures++;
        ctx.onvifPtz.lastResult = "GetPresets sem memoria para processar o catalogo";
        return false;
    }

    if (!received || !parseOnvifPtzPresets(responseXml, parsedPresets.get(), parsedCount, reportedCount, truncated)) {
        ctx.onvifPtz.refreshFailures++;
        ctx.onvifPtz.lastResult = transportFailure ? "GetPresets falhou no transporte" : String("GetPresets falhou; HTTP ") + statusCode;
        return false;
    }

    std::unique_ptr<OnvifPtzPreset[]> presets(parsedCount == 0 ? nullptr : new (std::nothrow) OnvifPtzPreset[parsedCount]);
    if (parsedCount != 0 && !presets) {
        ctx.onvifPtz.lastResult = "Memoria insuficiente para o catalogo PTZ";
        ctx.onvifPtz.refreshFailures++;
        return false;
    }
    for (uint16_t index = 0; index < parsedCount; index++)
        presets[index] = std::move(parsedPresets[index]);
    ctx.onvifPtz.presets = std::move(presets);

    ctx.onvifPtz.presetsLoaded = true;
    ctx.onvifServices.ptz.validated = true;
    ctx.onvifPtz.truncated = truncated;
    ctx.onvifPtz.presetCount = parsedCount;
    ctx.onvifPtz.reportedPresetCount = reportedCount;
    ctx.onvifPtz.refreshSuccesses++;
    ctx.onvifPtz.refreshedAtMs = millis();
    ctx.onvifPtz.lastResult = currentTimestamp() + " - GetPresets retornou " + String(reportedCount) + " posicoes";

    if (truncated) {
        ctx.onvifPtz.lastResult += "; exibindo as primeiras ";
        ctx.onvifPtz.lastResult += ONVIF_PTZ_PRESET_CAPACITY;
    }

    Serial.println(ctx.onvifPtz.lastResult);
    return true;
}

void maintainOnvifPtzPresets(CameraContext& ctx)
{
    if (ctx.lastPreviewRequestAtMs == 0 || millis() - ctx.lastPreviewRequestAtMs > 120000UL)
        return;
    if (!ctx.camera.valid || WiFi.status() != WL_CONNECTED || !ctx.onvifServices.resolved || !ctx.onvifServices.ptz.available || cameraRebootGraceActive(ctx) || healthCheckInProgress) {
        return;
    }

    const unsigned long intervalMs = ctx.onvifPtz.presetsLoaded ? ONVIF_PTZ_PRESET_REFRESH_INTERVAL_MS : ONVIF_PTZ_PRESET_RETRY_INTERVAL_MS;

    if (ctx.onvifPtz.lastRefreshAttemptAtMs != 0 && millis() - ctx.onvifPtz.lastRefreshAttemptAtMs < intervalMs) {
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        return;
    }

    refreshOnvifPtzPresets(ctx);
}

bool requestOnvifPtzPreset(CameraContext& ctx, const String& presetToken, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        ctx.onvifPtz.lastCommandResult = currentTimestamp() + " - canal da camera ocupado";
        return false;
    }

    if (!ctx.camera.valid || !ctx.onvifServices.ptz.available || cameraRebootGraceActive(ctx)) {
        ctx.onvifPtz.lastCommandResult = currentTimestamp() + " - servico PTZ indisponivel";
        return false;
    }

    if ((!ctx.onvifPtz.presetsLoaded || !ctx.onvifPtz.profileResolved) && !refreshOnvifPtzPresets(ctx, logContextId)) {
        ctx.onvifPtz.lastCommandResult = currentTimestamp() + " - presets PTZ indisponiveis";
        return false;
    }

    const int presetIndex = onvifPtzPresetIndex(ctx, presetToken);

    if (presetIndex < 0) {
        ctx.onvifPtz.lastCommandResult = currentTimestamp() + " - preset nao encontrado";
        return false;
    }

    const OnvifPtzPreset& preset = ctx.onvifPtz.presets[presetIndex];
    String requestBody;
    requestBody.reserve(ctx.onvifPtz.profileToken.length() + preset.token.length() + 176);
    requestBody += "<tptz:GotoPreset><tptz:ProfileToken>";
    requestBody += htmlEscape(ctx.onvifPtz.profileToken);
    requestBody += "</tptz:ProfileToken><tptz:PresetToken>";
    requestBody += htmlEscape(preset.token);
    requestBody += "</tptz:PresetToken></tptz:GotoPreset>";

    ctx.onvifPtz.commandAttempts++;
    ctx.onvifPtz.lastCommandAtMs = millis();
    ctx.onvifPtz.lastCommandToken = preset.token;
    const unsigned long startedAtMs = millis();
    String responseXml;
    int statusCode = 0;
    const bool accepted = postOnvifSoap(ctx, ctx.onvifServices.ptz.url, "http://www.onvif.org/ver20/ptz/wsdl/GotoPreset", requestBody, responseXml, false, &statusCode, nullptr, ctx.settings.serviceResponseTimeoutMs, logContextId);
    ctx.onvifPtz.lastCommandLatencyMs = millis() - startedAtMs;
    ctx.onvifPtz.lastCommandStatusCode = statusCode;

    if (accepted) {
        ctx.onvifPtz.commandAccepted++;
        ctx.lastBrowserSnapshotDeliveredAtMs = 0;
        ctx.onvifPtz.lastCommandResult = currentTimestamp() + " - comando para " + onvifPtzPresetLabel(preset) + " aceito pela câmera";
    } else {
        ctx.onvifPtz.commandErrors++;
        ctx.onvifPtz.lastCommandResult = currentTimestamp() + " - comando para " + onvifPtzPresetLabel(preset) + " rejeitado; HTTP " + statusCode;
    }

    Serial.println(ctx.onvifPtz.lastCommandResult);
    PersistentLog::record(PersistentLog::Event::PtzCommand, accepted ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, preset.token.c_str(), ctx.camera.ip, ctx.camera.onvifPort, statusCode, ctx.onvifPtz.lastCommandLatencyMs, static_cast<uint32_t>(presetIndex), PersistentLog::FlagManual, accepted ? PersistentLog::Level::Info : PersistentLog::Level::Error, true, logContextId, ctx.id);
    return accepted;
}

void noteSnapshotRequestFailure(CameraContext& ctx, int statusCode)
{
    if (ctx.snapshotTargetFailureStreak < 255) {
        ctx.snapshotTargetFailureStreak++;
    }

    const bool permanentUriFailure = statusCode == 404 || statusCode == 410;
    const bool authenticationFailure = statusCode == 401 || statusCode == 403;
    const bool testingAlternateProfile = ctx.snapshotProfileFailoverPending && !ctx.snapshotProfileTokenToSkip.isEmpty() && ctx.snapshotTarget.profileToken != ctx.snapshotProfileTokenToSkip;

    if (testingAlternateProfile) {
        if (permanentUriFailure || ctx.snapshotTargetFailureStreak >= ctx.settings.snapshotFailuresBeforeReresolve) {
            resetSnapshotProfileFailover(ctx);
        }

        return;
    }

    if (permanentUriFailure || (!authenticationFailure && ctx.snapshotTargetFailureStreak >= ctx.settings.snapshotFailuresBeforeReresolve)) {
        ctx.snapshotProfileTokenToSkip = ctx.snapshotTarget.profileToken;
        ctx.snapshotProfileFailoverPending = true;
        invalidateSnapshotTarget(ctx);
        ctx.lastSnapshotResolveAttemptAtMs = 0;
        ctx.snapshotTargetFailureStreak = 0;
    }
}

bool probeCameraSnapshotJpeg(CameraContext& ctx, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    SnapshotProbe result;
    result.attempted = true;
    ctx.snapshotProbeRuns++;
    const unsigned long startedAt = millis();
    bool resolutionAttempted = false;

    if (!ctx.snapshotTarget.valid) {
        if (ctx.lastSnapshotResolveAttemptAtMs != 0 && millis() - ctx.lastSnapshotResolveAttemptAtMs < ctx.settings.snapshotResolveRetryIntervalMs) {
            result.detail = "Aguardando nova tentativa de resolver o snapshot";
        } else {
            resolutionAttempted = true;

            if (!resolveSnapshotTarget(ctx, logContextId)) {
                result.detail = ctx.lastSnapshotResult;
            }
        }
    }

    result.targetResolved = ctx.snapshotTarget.valid;

    if (!ctx.snapshotTarget.valid) {
        result.statusCode = ctx.lastSnapshotResolveStatusCode;
        result.httpResponded = result.statusCode >= 100;
        result.requestAttempted = resolutionAttempted;
        result.resolutionAttempted = resolutionAttempted;
        result.resolutionTransportFailure = resolutionAttempted && ctx.lastSnapshotResolveTransportFailure;
        result.conclusive = resolutionAttempted && result.statusCode >= 500 && result.statusCode <= 599;
        result.latencyMs = millis() - startedAt;

        if (resolutionAttempted) {
            ctx.snapshotProbeFailures++;
        }

        if (result.conclusive) {
            resetSnapshotProfileFailover(ctx);
        }

        ctx.lastSnapshotProbe = result;
        return false;
    }

    result.requestAttempted = true;
    result.conclusive = true;
    HTTPClient& request = ctx.cameraSnapshotHttpSession.request;
    const int statusCode = openAuthenticatedCameraGet(ctx, ctx.snapshotTarget.url, logContextId);

    result.statusCode = statusCode;
    result.latencyMs = millis() - startedAt;
    result.httpResponded = statusCode >= 100;

    if (statusCode < 200 || statusCode >= 300) {
        result.detail = statusCode < 0 ? String("Falha HTTP: ") + HTTPClient::errorToString(statusCode) : String("Camera retornou HTTP ") + String(statusCode);
        finishCameraSnapshotGet(ctx, false);
        noteSnapshotRequestFailure(ctx, statusCode);
        ctx.snapshotProbeFailures++;
        ctx.lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        ctx.lastSnapshotProbe = result;
        return false;
    }

    const int contentLength = request.getSize();

    if (contentLength == 0 || (contentLength > 0 && contentLength < static_cast<int>(MIN_VALID_SNAPSHOT_SIZE_BYTES)) || contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)) {
        result.detail = "Tamanho de snapshot invalido";
        finishCameraSnapshotGet(ctx, false);
        noteSnapshotRequestFailure(ctx, statusCode);
        ctx.snapshotProbeFailures++;
        ctx.lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        ctx.lastSnapshotProbe = result;
        return false;
    }

    WiFiClient* source = request.getStreamPtr();

    if (source == nullptr) {
        result.detail = "Fluxo HTTP do snapshot indisponivel";
        finishCameraSnapshotGet(ctx, false);
        noteSnapshotRequestFailure(ctx, statusCode);
        ctx.snapshotProbeFailures++;
        ctx.lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        ctx.lastSnapshotProbe = result;
        return false;
    }

    uint8_t buffer[SNAPSHOT_PROBE_BUFFER_SIZE];
    unsigned long lastDataAtMs = millis();
    uint8_t previousByte = 0;
    uint8_t firstByte = 0;
    uint8_t secondByte = 0;
    bool jpegStarted = false;
    size_t jpegEndOffset = 0;

    while (result.bytesRead < MAX_SNAPSHOT_SIZE_BYTES && millis() - startedAt < ctx.settings.snapshotHttpTimeoutMs + ctx.settings.tcpConnectTimeoutMs) {
        updateCameraAlertLed();
        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            size_t bytesToRead = min(availableBytes, sizeof(buffer));
            bytesToRead = min(bytesToRead, MAX_SNAPSHOT_SIZE_BYTES - result.bytesRead);

            if (contentLength > 0) {
                const size_t expectedBytes = static_cast<size_t>(contentLength);
                const size_t remainingBytes = expectedBytes > result.bytesRead ? expectedBytes - result.bytesRead : 0;
                bytesToRead = min(bytesToRead, remainingBytes);
            }

            if (bytesToRead == 0) {
                break;
            }

            const size_t bytesRead = source->readBytes(buffer, bytesToRead);

            if (bytesRead == 0) {
                break;
            }

            for (size_t index = 0; index < bytesRead; index++) {
                const size_t absoluteIndex = result.bytesRead + index;
                const uint8_t value = buffer[index];

                if (absoluteIndex == 0) {
                    firstByte = value;
                } else if (absoluteIndex == 1) {
                    secondByte = value;
                    jpegStarted = firstByte == 0xFF && secondByte == 0xD8;
                }

                if (jpegStarted && previousByte == 0xFF && value == 0xD9) {
                    jpegEndOffset = absoluteIndex + 1;
                }

                previousByte = value;
            }

            result.bytesRead += bytesRead;
            lastDataAtMs = millis();

            if (contentLength < 0 && jpegEndOffset > 0 && result.bytesRead >= MIN_VALID_SNAPSHOT_SIZE_BYTES) {
                break;
            }

            if (contentLength > 0 && result.bytesRead >= static_cast<size_t>(contentLength)) {
                break;
            }

            continue;
        }

        if (!source->connected() || millis() - lastDataAtMs >= ctx.settings.snapshotHttpTimeoutMs) {
            break;
        }

        delay(1);
    }

    result.latencyMs = millis() - startedAt;

    const bool completeLength = contentLength < 0 || result.bytesRead == static_cast<size_t>(contentLength);
    const bool jpegEndNearTail = jpegEndOffset > 0 && result.bytesRead >= jpegEndOffset && result.bytesRead - jpegEndOffset <= 16;

    result.jpegValid = jpegStarted && jpegEndNearTail && completeLength && result.bytesRead >= MIN_VALID_SNAPSHOT_SIZE_BYTES;
    finishCameraSnapshotGet(ctx, result.jpegValid && contentLength > 0 && result.bytesRead == static_cast<size_t>(contentLength));

    if (result.jpegValid) {
        ctx.snapshotTargetFailureStreak = 0;
        resetSnapshotProfileFailover(ctx);
        result.detail = String("JPEG valido; ") + String(result.bytesRead) + " bytes";
        ctx.lastSnapshotResult = currentTimestamp() + " - " + result.detail + "; perfil " + ctx.snapshotTarget.profileName;
        ctx.lastSnapshotProbe = result;
        return true;
    }

    if (!jpegStarted) {
        result.detail = "Resposta nao iniciou com marcador JPEG FF D8";
    } else if (!completeLength) {
        result.detail = String("Transferencia incompleta; recebidos ") + String(result.bytesRead) + " de " + String(contentLength) + " bytes";
    } else if (jpegEndOffset == 0) {
        result.detail = "Snapshot incompleto; marcador FF D9 nao recebido";
    } else if (!jpegEndNearTail) {
        result.detail = "Marcador JPEG FF D9 apareceu antes do fim da resposta";
    } else {
        result.detail = "Snapshot JPEG menor que o limite minimo";
    }

    noteSnapshotRequestFailure(ctx, statusCode);
    ctx.snapshotProbeFailures++;
    ctx.lastSnapshotResult = currentTimestamp() + " - " + result.detail;
    ctx.lastSnapshotProbe = result;
    return false;
}

bool snapshotFailureAllowsAutomaticReboot(CameraContext& ctx)
{
    if (!ctx.lastSnapshotProbe.requestAttempted || !ctx.lastSnapshotProbe.conclusive || ctx.snapshotProfileFailoverPending) {
        return false;
    }

    if ((ctx.lastSnapshotProbe.statusCode >= 400 && ctx.lastSnapshotProbe.statusCode <= 499) || ctx.lastSnapshotProbe.statusCode == 501) {
        return false;
    }

    return !containsIgnoreCase(ctx.lastSnapshotProbe.detail, "nenhum perfil") && !containsIgnoreCase(ctx.lastSnapshotProbe.detail, "uri de snapshot invalido") && !containsIgnoreCase(ctx.lastSnapshotProbe.detail, "https local ainda nao suportado");
}

void recordCameraRebootEvidence(CameraContext& ctx, bool manualRequest)
{
    CameraRebootEvidence& evidence = ctx.cameraRebootHistory[ctx.cameraRebootHistoryNext];
    evidence = CameraRebootEvidence{};
    copySettingText(evidence.timestamp, currentTimestamp());
    copySettingText(evidence.snapshotProfile, ctx.snapshotTarget.profileName);
    evidence.uptimeSeconds = millis() / 1000UL;
    evidence.snapshotStatus = static_cast<int16_t>(ctx.lastSnapshotProbe.statusCode);
    evidence.snapshotLatencyMs = ctx.lastSnapshotProbe.latencyMs;
    evidence.snapshotBytes = static_cast<uint32_t>(ctx.lastSnapshotProbe.bytesRead);
    evidence.consecutiveImageFailures = ctx.consecutiveFailures;
    evidence.esp32WifiRssi = static_cast<int16_t>(WiFi.RSSI());
    evidence.freeHeapBytes = ESP.getFreeHeap();
    evidence.manual = manualRequest;

    ctx.cameraRebootHistoryNext = (ctx.cameraRebootHistoryNext + 1U) % CAMERA_REBOOT_HISTORY_CAPACITY;
    if (ctx.cameraRebootHistoryCount < CAMERA_REBOOT_HISTORY_CAPACITY) {
        ctx.cameraRebootHistoryCount++;
    }
}

bool requestCameraRebootOnvif(CameraContext& ctx, bool manualRequest, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    const unsigned long rebootStartedAtMs = millis();
    const auto recordReboot = [&](PersistentLog::Outcome outcome, const char* detail, int statusCode, PersistentLog::Level level, bool critical) {
        PersistentLog::record(PersistentLog::Event::CameraReboot, outcome, detail, ctx.camera.ip, ctx.camera.onvifPort, statusCode, millis() - rebootStartedAtMs, ctx.cameraRebootAttempts, manualRequest ? PersistentLog::FlagManual : PersistentLog::FlagScheduled, level, critical, logContextId, ctx.id);
    };
    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        ctx.lastCameraRebootResult = currentTimestamp() + " - canal da camera ocupado";
        recordReboot(PersistentLog::Outcome::Skipped, "camera_io_busy", 0, PersistentLog::Level::Warning, false);
        return false;
    }

    if (!ctx.camera.valid) {
        ctx.lastCameraRebootResult = currentTimestamp() + " - camera ainda nao localizada";
        recordReboot(PersistentLog::Outcome::Skipped, "camera_not_found", 0, PersistentLog::Level::Warning, false);
        return false;
    }

    if (!manualRequest && !ctx.settings.enableOnvifAutoReboot) {
        ctx.lastCameraRebootResult = currentTimestamp() + " - reboot automatico desativado";
        recordReboot(PersistentLog::Outcome::Skipped, "automatic_disabled", 0, PersistentLog::Level::Info, false);
        return false;
    }

    refreshCameraRebootWindow(ctx);
    const unsigned long now = millis();

    if (!manualRequest && now - ctx.registeredAtMs < ctx.settings.startupGraceMs) {
        const unsigned long remainingMs = ctx.settings.startupGraceMs - (now - ctx.registeredAtMs);
        ctx.lastCameraRebootResult = currentTimestamp() + " - reboot aguardando a tolerancia inicial; faltam " + durationText(remainingMs);
        recordReboot(PersistentLog::Outcome::Skipped, "startup_grace", 0, PersistentLog::Level::Info, false);
        return false;
    }

    if (cameraRebootGraceActive(ctx)) {
        recordReboot(PersistentLog::Outcome::Skipped, "reboot_grace", 0, PersistentLog::Level::Info, false);
        return false;
    }

    if (ctx.lastCameraRebootAttemptAtMs != 0 && now - ctx.lastCameraRebootAttemptAtMs < ctx.settings.onvifRebootCooldownMs) {
        const unsigned long remainingMs = ctx.settings.onvifRebootCooldownMs - (now - ctx.lastCameraRebootAttemptAtMs);
        ctx.lastCameraRebootResult = currentTimestamp() + " - novo reboot bloqueado por cooldown; faltam " + durationText(remainingMs);
        recordReboot(PersistentLog::Outcome::Skipped, "cooldown", 0, PersistentLog::Level::Warning, false);
        return false;
    }

    if (ctx.cameraRebootAttemptsInWindow >= ctx.settings.maxOnvifRebootAttemptsPerWindow) {
        const unsigned long elapsedMs = now - ctx.cameraRebootWindowStartedAtMs;
        const unsigned long remainingMs = elapsedMs < ctx.settings.onvifRebootWindowMs ? ctx.settings.onvifRebootWindowMs - elapsedMs : 0;
        ctx.lastCameraRebootResult = currentTimestamp() + " - limite de tentativas atingido; nova janela em " + durationText(remainingMs);
        recordReboot(PersistentLog::Outcome::Skipped, "window_limit", 0, PersistentLog::Level::Warning, true);
        return false;
    }

    ctx.lastCameraRebootAttemptAtMs = now;
    ctx.cameraRebootAttempts++;

    if (manualRequest) {
        ctx.manualCameraRebootAttempts++;
    } else {
        ctx.automaticCameraRebootAttempts++;
    }

    String responseBody;
    int statusCode = 0;
    ctx.cameraRebootAcceptedAtMs[ctx.cameraRebootAttemptsInWindow++] = now;
    ctx.cameraRebootWindowStartedAtMs = ctx.cameraRebootAcceptedAtMs[0];
    if (!persistRebootLimiter(ctx)) {
        ctx.cameraRebootAttemptsInWindow--;
        ctx.cameraRebootAcceptedAtMs[ctx.cameraRebootAttemptsInWindow] = 0;
        ctx.lastCameraRebootResult = currentTimestamp() + " - NVS indisponivel; reboot bloqueado para preservar limite";
        recordReboot(PersistentLog::Outcome::Skipped, "reboot_ledger_failed", 0, PersistentLog::Level::Error, true);
        return false;
    }
    const bool accepted = postOnvifSoap(ctx, ctx.camera.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/SystemReboot", "<tds:SystemReboot/>", responseBody, false, &statusCode, nullptr, ctx.settings.serviceResponseTimeoutMs, logContextId);

    if (!accepted) {
        ctx.cameraRebootErrors++;

        if (manualRequest) {
            ctx.manualCameraRebootErrors++;
        } else {
            ctx.automaticCameraRebootErrors++;
        }

        ctx.lastCameraRebootResult = currentTimestamp() + " - ";
        ctx.lastCameraRebootResult += manualRequest ? "manual" : "automatico";
        ctx.lastCameraRebootResult += " rejeitado; ";

        if (statusCode < 0) {
            ctx.lastCameraRebootResult += HTTPClient::errorToString(statusCode);
        } else {
            ctx.lastCameraRebootResult += "HTTP ";
            ctx.lastCameraRebootResult += statusCode;
        }

        String faultText;
        int ignoredPosition = 0;
        extractLocalTag(responseBody, "Text", 0, faultText, ignoredPosition);

        if (!faultText.isEmpty()) {
            ctx.lastCameraRebootResult += "; ";
            ctx.lastCameraRebootResult += faultText;
        }

        if (statusCode == 401 && !cameraCredentialsConfigured(ctx)) {
            ctx.lastCameraRebootResult += "; configure a autenticacao da camera no painel";
        }

        Serial.println(ctx.lastCameraRebootResult);
        recordReboot(PersistentLog::Outcome::Failure, "SystemReboot", statusCode, PersistentLog::Level::Error, true);
        return false;
    }

    ctx.cameraRebootAccepted++;

    if (manualRequest) {
        ctx.manualCameraRebootAccepted++;
    } else {
        ctx.automaticCameraRebootAccepted++;
    }

    ctx.lastCameraRebootEvidence = currentTimestamp() + " - origem ";
    ctx.lastCameraRebootEvidence += manualRequest ? "manual" : "automatica";
    ctx.lastCameraRebootEvidence += "; JPEG HTTP ";
    ctx.lastCameraRebootEvidence += ctx.lastSnapshotProbe.statusCode;
    ctx.lastCameraRebootEvidence += ", ";
    ctx.lastCameraRebootEvidence += ctx.lastSnapshotProbe.bytesRead;
    ctx.lastCameraRebootEvidence += " bytes em ";
    ctx.lastCameraRebootEvidence += ctx.lastSnapshotProbe.latencyMs;
    ctx.lastCameraRebootEvidence += " ms; falhas JPEG ";
    ctx.lastCameraRebootEvidence += ctx.consecutiveFailures;
    ctx.lastCameraRebootEvidence += "; Wi-Fi ESP32 ";
    ctx.lastCameraRebootEvidence += WiFi.RSSI();
    ctx.lastCameraRebootEvidence += " dBm; heap livre ";
    ctx.lastCameraRebootEvidence += ESP.getFreeHeap();
    ctx.lastCameraRebootEvidence += " bytes";
    recordCameraRebootEvidence(ctx, manualRequest);

    const unsigned long acceptedAtMs = millis();
    ctx.cameraRebootAcceptedAtMs[ctx.cameraRebootAttemptsInWindow - 1] = acceptedAtMs;
    ctx.cameraRebootGraceStartedAtMs = acceptedAtMs;
    invalidateSnapshotTarget(ctx);
    invalidateOnvifServiceCatalog(ctx);
    resetSnapshotProfileFailover(ctx);
    ctx.lastSnapshotResolveAttemptAtMs = now;
    ctx.snapshotTargetFailureStreak = 0;
    ctx.consecutiveFailures = 0;
    ctx.consecutiveSuccesses = 0;
    ctx.consecutiveControlFailures = 0;
    ctx.consecutiveInconclusiveChecks = 0;
    ctx.consecutiveMediaServiceFailures = 0;
    ctx.lastSnapshotResolveStatusCode = 0;
    ctx.lastSnapshotResolveTransportFailure = false;
    ctx.lastSnapshotProbe = SnapshotProbe{};
    ctx.lastSnapshotProbe.attempted = true;
    ctx.lastSnapshotProbe.detail = "Aguardando a camera reiniciar";

    String rebootMessage;
    int ignoredPosition = 0;
    extractLocalTag(responseBody, "Message", 0, rebootMessage, ignoredPosition);

    ctx.lastCameraRebootResult = currentTimestamp() + " - ";
    ctx.lastCameraRebootResult += manualRequest ? "manual" : "automatico";
    ctx.lastCameraRebootResult += " comando aceito; reinicio ainda nao confirmado; HTTP ";
    ctx.lastCameraRebootResult += statusCode;

    if (!rebootMessage.isEmpty()) {
        ctx.lastCameraRebootResult += "; ";
        ctx.lastCameraRebootResult += rebootMessage;
    }

    setState(ctx, CameraState::Recovering, "Reinicializacao ONVIF solicitada; aguardando a camera");
    Serial.println(ctx.lastCameraRebootResult);
    recordReboot(PersistentLog::Outcome::Success, "SystemReboot", statusCode, PersistentLog::Level::Info, true);
    return true;
}

void sendSnapshotError(CameraContext& ctx, int statusCode, const char* message, bool countError = true)
{
    if (countError) {
        ctx.snapshotErrors++;
    }

    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(statusCode, "application/json; charset=utf-8", String("{\"error\":\"") + message + "\"}");
}

void serveCameraSnapshot(CameraContext& ctx)
{
    ctx.lastPreviewRequestAtMs = millis();
    if (healthCheckInProgress || registryBusy || cameraIoDepth != 0 || anyCameraHealthCheckDue()) {
        ctx.previewBusyResponses++;
        sendSnapshotError(ctx, 503, "health_check_in_progress", false);
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        ctx.previewBusyResponses++;
        sendSnapshotError(ctx, 503, "camera_io_busy", false);
        return;
    }

    const uint16_t logContextId = allocateLogContextId();
    const unsigned long snapshotStartedAtMs = millis();

    if (!ctx.snapshotTarget.valid) {
        if (ctx.lastSnapshotResolveAttemptAtMs != 0 && millis() - ctx.lastSnapshotResolveAttemptAtMs < ctx.settings.snapshotResolveRetryIntervalMs) {
            sendSnapshotError(ctx, 503, "snapshot_temporarily_unavailable");
            return;
        }

        if (!resolveSnapshotTarget(ctx, logContextId)) {
            sendSnapshotError(ctx, 503, "snapshot_unavailable");
            return;
        }
    }

    HTTPClient& request = ctx.cameraSnapshotHttpSession.request;
    ctx.previewCameraFetches++;
    const int statusCode = openAuthenticatedCameraGet(ctx, ctx.snapshotTarget.url, logContextId);
    const int contentLength = request.getSize();

    if (statusCode < 200 || statusCode >= 300 || contentLength == 0 || (contentLength > 0 && contentLength < static_cast<int>(MIN_VALID_SNAPSHOT_SIZE_BYTES)) || contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)) {
        finishCameraSnapshotGet(ctx, false);
        ctx.lastSnapshotResult = statusCode < 0 ? currentTimestamp() + " - " + HTTPClient::errorToString(statusCode) : currentTimestamp() + " - falha HTTP " + String(statusCode);
        sendSnapshotError(ctx, 502, "invalid_camera_response");
        return;
    }

    WiFiClient* source = request.getStreamPtr();

    if (source == nullptr) {
        finishCameraSnapshotGet(ctx, false);
        sendSnapshotError(ctx, 502, "camera_stream_unavailable");
        return;
    }

    uint8_t prefix[2] = {0, 0};
    size_t prefixLength = 0;
    unsigned long lastDataAtMs = millis();

    while (prefixLength < sizeof(prefix) && millis() - snapshotStartedAtMs < ctx.settings.snapshotHttpTimeoutMs + ctx.settings.tcpConnectTimeoutMs) {
        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            const int value = source->read();

            if (value < 0) {
                break;
            }

            prefix[prefixLength++] = static_cast<uint8_t>(value);
            lastDataAtMs = millis();
            continue;
        }

        if (!source->connected() || millis() - lastDataAtMs >= ctx.settings.snapshotHttpTimeoutMs) {
            break;
        }

        updateCameraAlertLed();
        delay(1);
    }

    if (prefixLength != sizeof(prefix) || prefix[0] != 0xFF || prefix[1] != 0xD8) {
        finishCameraSnapshotGet(ctx, false);
        ctx.lastSnapshotResult = "Resposta da camera nao e um JPEG valido";
        sendSnapshotError(ctx, 502, "invalid_jpeg_header");
        return;
    }

    httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    httpServer.sendHeader("Pragma", "no-cache");
    httpServer.sendHeader("X-Content-Type-Options", "nosniff");

    if (contentLength > 0) {
        httpServer.setContentLength(static_cast<size_t>(contentLength));
    } else {
        httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    }

    httpServer.send(200, "image/jpeg", "");
    httpServer.sendContent(reinterpret_cast<const char*>(prefix), sizeof(prefix));

    WiFiClient browserClient = httpServer.client();
    const IPAddress browserIp = browserClient.remoteIP();
    const uint16_t browserPort = browserClient.remotePort();
    uint8_t transferBuffer[SNAPSHOT_TRANSFER_BUFFER_SIZE];
    size_t bytesTransferred = sizeof(prefix);
    uint8_t previousByte = prefix[1];
    size_t jpegEndOffset = 0;
    bool browserDisconnected = false;
    lastDataAtMs = millis();

    while (bytesTransferred < MAX_SNAPSHOT_SIZE_BYTES && millis() - snapshotStartedAtMs < ctx.settings.snapshotHttpTimeoutMs + ctx.settings.tcpConnectTimeoutMs) {
        updateCameraAlertLed();

        if (!browserClient.connected()) {
            browserDisconnected = true;
            break;
        }

        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            size_t bytesToRead = min(availableBytes, sizeof(transferBuffer));

            if (contentLength > 0) {
                const size_t remaining = static_cast<size_t>(contentLength) - bytesTransferred;
                bytesToRead = min(bytesToRead, remaining);
            } else {
                bytesToRead = min(bytesToRead, MAX_SNAPSHOT_SIZE_BYTES - bytesTransferred);
            }

            if (bytesToRead == 0) {
                break;
            }

            const size_t bytesRead = source->readBytes(transferBuffer, bytesToRead);

            if (bytesRead == 0) {
                break;
            }

            for (size_t index = 0; index < bytesRead; index++) {
                const uint8_t value = transferBuffer[index];

                if (previousByte == 0xFF && value == 0xD9) {
                    jpegEndOffset = bytesTransferred + index + 1;
                }

                previousByte = value;
            }

            httpServer.sendContent(reinterpret_cast<const char*>(transferBuffer), bytesRead);
            bytesTransferred += bytesRead;
            lastDataAtMs = millis();

            if (contentLength > 0 && bytesTransferred >= static_cast<size_t>(contentLength)) {
                break;
            }

            if (contentLength < 0 && jpegEndOffset > 0) {
                break;
            }

            continue;
        }

        if (!source->connected() || millis() - lastDataAtMs >= ctx.settings.snapshotHttpTimeoutMs) {
            break;
        }

        delay(1);
    }

    if (contentLength < 0 && browserClient.connected()) {
        httpServer.sendContent("");
    }

    const bool completeLength = contentLength < 0 || bytesTransferred == static_cast<size_t>(contentLength);
    const bool jpegEndNearTail = jpegEndOffset > 0 && bytesTransferred >= jpegEndOffset && bytesTransferred - jpegEndOffset <= 16;
    const bool validTransfer = !browserDisconnected && completeLength && jpegEndNearTail && bytesTransferred >= MIN_VALID_SNAPSHOT_SIZE_BYTES;
    finishCameraSnapshotGet(ctx, validTransfer && contentLength > 0 && bytesTransferred == static_cast<size_t>(contentLength));

    if (validTransfer) {
        ctx.snapshotsServed++;
        ctx.previewBytesTransferred += bytesTransferred;
        ctx.lastBrowserSnapshotDeliveredAtMs = millis();
        ctx.lastBrowserSnapshotBytes = bytesTransferred;
        ctx.lastBrowserSnapshotLatencyMs = ctx.lastBrowserSnapshotDeliveredAtMs - snapshotStartedAtMs;
        ctx.lastSnapshotResult = currentTimestamp() + " - quadro entregue; " + String(bytesTransferred) + " bytes; perfil " + ctx.snapshotTarget.profileName;
    } else if (browserDisconnected) {
        ctx.previewClientAborts++;
    } else {
        ctx.snapshotErrors++;
        ctx.lastSnapshotResult = "Transferencia do snapshot JPEG incompleta";
    }

    const unsigned long transferFinishedAtMs = millis();
    const bool logTransfer = !validTransfer || ctx.lastSuccessfulSnapshotTransferLogAtMs == 0 || transferFinishedAtMs - ctx.lastSuccessfulSnapshotTransferLogAtMs >= SNAPSHOT_SUCCESS_LOG_INTERVAL_MS;

    if (logTransfer) {
        PersistentLog::record(PersistentLog::Event::SnapshotTransfer, validTransfer ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, validTransfer ? "browser_jpeg" : browserDisconnected ? "browser_disconnected" : "incomplete_jpeg", browserIp, browserPort, 200, transferFinishedAtMs - snapshotStartedAtMs, static_cast<uint32_t>(bytesTransferred), PersistentLog::FlagNone, PersistentLog::Level::Automatic, false, logContextId, ctx.id);

        if (validTransfer) {
            ctx.lastSuccessfulSnapshotTransferLogAtMs = transferFinishedAtMs;
        }
    }
}

ServiceProbe probeRtsp(CameraContext& ctx, const IPAddress& ip, uint16_t port, uint16_t contextId)
{
    ServiceProbe result;
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    const String requestUri = String("rtsp://") + ip.toString() + ':' + String(port) + '/';

    WiFiClient client;
    const unsigned long startedAt = millis();
    const TcpConnectionAttempt connection = connectTcp(client, ip, port, ctx.settings.tcpConnectTimeoutMs);

    if (!connection.connected) {
        client.stop();
        result.latencyMs = connection.durationMs;
        PersistentLog::record(PersistentLog::Event::RtspProbe, PersistentLog::Outcome::Failure, connection.detail, ip, port, connection.errorCode, result.latencyMs, ctx.settings.tcpConnectTimeoutMs, PersistentLog::FlagOutbound, PersistentLog::Level::Error, false, logContextId, ctx.id);
        return result;
    }

    result.tcpConnected = true;
    result.latencyMs = millis() - startedAt;

    client.print("OPTIONS ");
    client.print(requestUri);
    client.print(" RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: ESP32-Camera-Watchdog/");
    client.print(FIRMWARE_VERSION);

    client.print("\r\n\r\n");

    const unsigned long responseStartedAt = millis();
    String firstLine;
    firstLine.reserve(96);

    while (millis() - responseStartedAt < ctx.settings.serviceResponseTimeoutMs) {
        updateCameraAlertLed();

        while (client.available()) {
            const char value = static_cast<char>(client.read());

            if (value == '\n') {
                break;
            }

            if (value != '\r' && firstLine.length() < 200) {
                firstLine += value;
            }
        }

        if (!firstLine.isEmpty() && firstLine.indexOf("RTSP/") == 0) {
            result.protocolResponded = true;
            const int firstSpace = firstLine.indexOf(' ');

            if (firstSpace >= 0) {
                result.statusCode = firstLine.substring(firstSpace + 1).toInt();
            }

            break;
        }

        if (!client.connected() && !client.available()) {
            break;
        }

        httpServer.handleClient();
        delay(1);
    }

    client.stop();
    result.latencyMs = millis() - startedAt;
    const char* resultDetail = result.protocolResponded ? "OPTIONS" : "invalid_response";
    PersistentLog::record(PersistentLog::Event::RtspProbe, result.protocolResponded ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, resultDetail, ip, port, result.statusCode, result.latencyMs, ctx.settings.tcpConnectTimeoutMs, PersistentLog::FlagOutbound, result.protocolResponded ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, logContextId, ctx.id);
    return result;
}

bool tcpPortOpen(const IPAddress& ip, uint16_t port, uint32_t timeoutMs, uint16_t contextId = 0)
{
    WiFiClient client;
    const TcpConnectionAttempt connection = connectTcp(client, ip, port, timeoutMs);
    client.stop();
    PersistentLog::record(PersistentLog::Event::TcpScan, connection.connected ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Info, connection.connected ? "port_open" : connection.detail, ip, port, connection.errorCode, connection.durationMs, timeoutMs, PersistentLog::FlagOutbound, PersistentLog::Level::Info, false, contextId);
    return connection.connected;
}

void maintainSubnetScan()
{
    static uint32_t cursor = 0;
    static uint32_t broadcast = 0;
    static IPAddress candidateIp;
    static size_t portIndex = 0;
    static bool checkingOnvif = false;
    static uint16_t contextId = 0;
    static unsigned long startedAtMs = 0;
    static unsigned long lastStepAtMs = 0;
    if (WiFi.status() != WL_CONNECTED || !globalSettings.enableAutomaticSubnetScan) {
        cursor = 0;
        checkingOnvif = false;
        subnetScanRequested = false;
        return;
    }
    if (anyCameraHealthCheckDue() || currentAdminAction() != AdminAction::None || registryBusy || millis() - lastStepAtMs < 50UL)
        return;
    if (cursor == 0) {
        if (!subnetScanRequested || (lastSubnetScanAtMs != 0 && millis() - lastSubnetScanAtMs < globalSettings.subnetScanIntervalMs))
            return;
        const uint32_t local = ipToUint32(WiFi.localIP());
        const uint32_t mask = ipToUint32(WiFi.subnetMask());
        const uint32_t network = local & mask;
        broadcast = network | ~mask;
        subnetScanRequested = false;
        lastSubnetScanAtMs = millis();
        if (broadcast <= network + 1 || broadcast - network - 1 > MAX_SCAN_HOSTS) {
            lastScanResult = "Sub-rede fora do limite de hosts da varredura";
            return;
        }
        cursor = network + 1;
        startedAtMs = millis();
        contextId = allocateLogContextId();
        subnetScanRuns++;
        lastScanResult = "Varredura incremental em andamento";
    }
    lastStepAtMs = millis();
    CameraIoGuard ioGuard;
    if (!ioGuard.acquired())
        return;
    if (!checkingOnvif) {
        if (cursor >= broadcast) {
            cursor = 0;
            lastScanResult = currentTimestamp() + " - varredura concluida";
            return;
        }
        candidateIp = uint32ToIp(cursor++);
        if (candidateIp == WiFi.localIP() || candidateIp == WiFi.gatewayIP())
            return;
        for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
            if (ctx->camera.ip == candidateIp)
                return;
        }
        if (!tcpPortOpen(candidateIp, CAMERA_RTSP_PORT, globalSettings.scanConnectTimeoutMs, contextId))
            return;
        checkingOnvif = true;
        portIndex = 0;
    }
    if (ESP.getFreeHeap() < sizeof(CameraContext) + CAMERA_OPERATION_HEAP_RESERVE) {
        lastScanResult = "Varredura adiada por memoria insuficiente";
        return;
    }
    std::unique_ptr<CameraContext> candidate(new (std::nothrow) CameraContext());
    if (!candidate)
        return;
    candidate->camera.valid = true;
    candidate->camera.ip = candidateIp;
    candidate->camera.onvifPort = ONVIF_PORT_CANDIDATES[portIndex++];
    candidate->camera.onvifPath = DEFAULT_ONVIF_PATH;
    candidate->camera.onvifUrl = makeOnvifUrl(candidateIp, candidate->camera.onvifPort, DEFAULT_ONVIF_PATH);
    candidate->settings.tcpConnectTimeoutMs = globalSettings.scanConnectTimeoutMs;
    candidate->settings.serviceResponseTimeoutMs = globalSettings.scanConnectTimeoutMs;
    const ServiceProbe probe = probeOnvif(*candidate, candidate->camera, contextId);
    if (probe.protocolResponded) {
        enqueueDiscoveryIdentityCandidate(candidate->camera);
        checkingOnvif = false;
    } else if (portIndex >= ONVIF_PORT_CANDIDATE_COUNT) {
        checkingOnvif = false;
    }
}

}
