#include "firmware_internal.h"

namespace CameraWatchdog {

constexpr char NVS_NAMESPACE[] = "camwatch";
constexpr unsigned long ONVIF_CATALOG_FALLBACK_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long ONVIF_PTZ_PRESET_REFRESH_INTERVAL_MS = 60UL * 1000UL;
constexpr unsigned long ONVIF_PTZ_PRESET_RETRY_INTERVAL_MS = 15UL * 1000UL;
constexpr unsigned long CAMERA_CLOCK_CHECK_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long CAMERA_CLOCK_RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t CAMERA_CLOCK_MAX_ROUND_TRIP_MS = 2000UL;

const IPAddress WS_DISCOVERY_MULTICAST(239, 255, 255, 250);

constexpr char SOAP_GET_SYSTEM_TIME[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" " "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">" "<s:Body><tds:GetSystemDateAndTime/></s:Body>" "</s:Envelope>";

bool postOnvifSoap(const String& serviceUrl, const char* action, const String& body, String& responseBody, bool requireResponseBody = true, int* finalStatusCode = nullptr, bool* transportFailure = nullptr, uint32_t timeoutMs = SERVICE_RESPONSE_TIMEOUT_MS, uint16_t contextId = 0);

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

String makeDigestAuthorization(const char* method, const String& url, const DigestChallenge& challenge, uint32_t nonceCountValue = 1)
{
    if (!cameraCredentialsConfigured() || !challenge.valid) {
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

    String ha1 = md5Hex(String(CAMERA_ONVIF_USERNAME_VALUE) + ':' + challenge.realm + ':' + CAMERA_ONVIF_PASSWORD_VALUE);

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
    authorization += httpQuotedEscape(CAMERA_ONVIF_USERNAME_VALUE);
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

void applyCameraAuthorization(HTTPClient& request, const String& digestAuthorization)
{
    if (!digestAuthorization.isEmpty()) {
        request.setAuthorizationType("Digest");
        request.setAuthorization(digestAuthorization.c_str());
        return;
    }

    if (cameraCredentialsConfigured()) {
        request.setAuthorizationType("Basic");
        request.setAuthorization(CAMERA_ONVIF_USERNAME_VALUE, CAMERA_ONVIF_PASSWORD_VALUE);
    }
}

void finishCameraSnapshotGet(bool responseCanBeReused)
{
    cameraSnapshotHttpSession.request.end();

    if (responseCanBeReused && cameraSnapshotHttpSession.client.connected()) {
        cameraSnapshotHttpSession.reusableAtMs = millis();
    } else {
        cameraSnapshotHttpSession.client.stop();
        cameraSnapshotHttpSession.reusableAtMs = 0;
    }
}

void maintainCameraSnapshotHttpSession()
{
    if (cameraSnapshotHttpSession.reusableAtMs != 0 && millis() - cameraSnapshotHttpSession.reusableAtMs >= SNAPSHOT_CONNECTION_REUSE_IDLE_MS) {
        finishCameraSnapshotGet(false);
    }
}

void resetCameraSnapshotHttpSession()
{
    finishCameraSnapshotGet(false);
    cameraSnapshotHttpSession.url = String();
    cameraSnapshotHttpSession.digestChallenge = DigestChallenge{};
    cameraSnapshotHttpSession.digestNonceCount = 0;
    cameraSnapshotHttpSession.forceHttp10 = false;
}

String makeNextCameraSnapshotDigestAuthorization(const String& url)
{
    CameraSnapshotHttpSession& session = cameraSnapshotHttpSession;

    if (!session.digestChallenge.valid) {
        return String();
    }

    const uint32_t nextNonceCount = session.digestNonceCount == 0xFFFFFFFFUL ? 1UL : session.digestNonceCount + 1UL;
    const String authorization = makeDigestAuthorization("GET", url, session.digestChallenge, nextNonceCount);

    if (authorization.isEmpty()) {
        session.digestChallenge = DigestChallenge{};
        session.digestNonceCount = 0;
        return String();
    }

    session.digestNonceCount = nextNonceCount;
    return authorization;
}

int openAuthenticatedCameraGet(const String& url, uint16_t contextId = 0)
{
    CameraSnapshotHttpSession& session = cameraSnapshotHttpSession;
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    ParsedUrl logEndpoint;
    parseHttpUrl(url, logEndpoint);

    if (session.url != url) {
        resetCameraSnapshotHttpSession();
        session.url = url;
    }

    HTTPClient& request = session.request;
    WiFiClient& client = session.client;
    const char* responseHeaders[] = {"Content-Type", "WWW-Authenticate", "Transfer-Encoding"};

    request.setConnectTimeout(TCP_CONNECT_TIMEOUT_MS);
    request.setTimeout(SNAPSHOT_HTTP_TIMEOUT_MS);
    request.useHTTP10(session.forceHttp10);
    request.setReuse(!session.forceHttp10);
    request.setUserAgent(String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION);
    request.collectHeaders(responseHeaders, 3);

    bool responseUsesChunkedTransfer = false;
    static unsigned long lastSuccessfulSnapshotHttpLogAtMs = 0;
    const auto executeAttempt = [&](const String& digestAuthorization, bool authenticationRetry) {
        responseUsesChunkedTransfer = false;
        const unsigned long attemptStartedAtMs = millis();

        if (!request.begin(client, url)) {
            PersistentLog::record(PersistentLog::Event::SnapshotHttp, PersistentLog::Outcome::Failure, "camera_jpeg", logEndpoint.ip, logEndpoint.port, -1, millis() - attemptStartedAtMs, 0, static_cast<uint8_t>(PersistentLog::FlagOutbound | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), PersistentLog::Level::Automatic, false, logContextId);
            return -1;
        }

        const bool connectionWasOpen = client.connected();
        cameraJpegHttpRequests++;
        request.addHeader("Accept", "image/jpeg, image/*;q=0.9, */*;q=0.1");
        applyCameraAuthorization(request, digestAuthorization);
        const int statusCode = request.GET();
        const bool requestAccepted = statusCode >= 200 && statusCode < 400;
        const unsigned long attemptFinishedAtMs = millis();
        const bool logAttempt = !requestAccepted || lastSuccessfulSnapshotHttpLogAtMs == 0 || attemptFinishedAtMs - lastSuccessfulSnapshotHttpLogAtMs >= SNAPSHOT_SUCCESS_LOG_INTERVAL_MS;

        if (logAttempt) {
            PersistentLog::record(PersistentLog::Event::SnapshotHttp, requestAccepted ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, "camera_jpeg", logEndpoint.ip, logEndpoint.port, statusCode, attemptFinishedAtMs - attemptStartedAtMs, statusCode > 0 ? static_cast<uint32_t>(max(request.getSize(), 0)) : 0, static_cast<uint8_t>(PersistentLog::FlagOutbound | (connectionWasOpen ? PersistentLog::FlagConnectionReused : PersistentLog::FlagNone) | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), statusCode == 401 && !authenticationRetry ? PersistentLog::Level::Warning : PersistentLog::Level::Info, false, logContextId);

            if (requestAccepted) {
                lastSuccessfulSnapshotHttpLogAtMs = attemptFinishedAtMs;
            }
        }

        if (connectionWasOpen && statusCode > 0) {
            cameraJpegConnectionReuses++;
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

    String digestAuthorization = makeNextCameraSnapshotDigestAuthorization(url);

    if (!digestAuthorization.isEmpty()) {
        cameraJpegDigestPreemptiveRequests++;
    }

    int statusCode = executeAttempt(digestAuthorization, !digestAuthorization.isEmpty());

    if (responseUsesChunkedTransfer && statusCode != 401) {
        finishCameraSnapshotGet(false);
        digestAuthorization = makeNextCameraSnapshotDigestAuthorization(url);
        statusCode = executeAttempt(digestAuthorization, !digestAuthorization.isEmpty());
    }

    if (statusCode != 401 || !cameraCredentialsConfigured()) {
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
    digestAuthorization = makeNextCameraSnapshotDigestAuthorization(url);

    if (digestAuthorization.isEmpty()) {
        session.digestChallenge = DigestChallenge{};
        session.digestNonceCount = 0;
        return statusCode;
    }

    finishCameraSnapshotGet(false);
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

void invalidateSnapshotTarget()
{
    resetCameraSnapshotHttpSession();
    snapshotTarget = SnapshotTarget{};
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

void resetSnapshotProfileFailover()
{
    snapshotProfileTokenToSkip = String();
    snapshotProfileFailoverPending = false;
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

void saveCameraTarget(uint16_t contextId, uint8_t flags)
{
    if (!camera.valid) {
        return;
    }

    if (!preferences.begin(NVS_NAMESPACE, false)) {
        PersistentLog::record(PersistentLog::Event::CameraTargetSave, PersistentLog::Outcome::Failure, "nvs_open_failed", camera.ip, camera.onvifPort, 0, 0, camera.rtspPort, flags, PersistentLog::Level::Error, true, contextId);
        return;
    }

    const size_t urlBytes = preferences.putString("cam_url", camera.onvifUrl);
    const size_t uuidBytes = preferences.putString("cam_uuid", camera.endpointUuid);
    const size_t scopeBytes = preferences.putString("cam_scope", camera.scopes);
    const size_t rtspBytes = preferences.putUShort("rtsp_port", camera.rtspPort);
    preferences.end();

    const bool saved = urlBytes == camera.onvifUrl.length() && uuidBytes == camera.endpointUuid.length() && scopeBytes == camera.scopes.length() && rtspBytes == sizeof(camera.rtspPort);
    PersistentLog::record(PersistentLog::Event::CameraTargetSave, saved ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, saved ? "nvs_target" : "nvs_write_failed", camera.ip, camera.onvifPort, saved ? 1 : 0, 0, camera.rtspPort, flags, saved ? PersistentLog::Level::Info : PersistentLog::Level::Error, true, contextId);
}

bool loadCameraTarget()
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

    camera.valid = true;
    camera.ip = parsed.ip;
    camera.onvifPort = parsed.port;
    camera.rtspPort = CAMERA_RTSP_PORT;
    camera.onvifPath = parsed.path;
    camera.onvifUrl = savedUrl;
    camera.endpointUuid = savedUuid;
    camera.scopes = savedScopes;

    Serial.printf("Camera carregada da NVS: %s\n", camera.onvifUrl.c_str());
    PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Success, "nvs_target", camera.ip, camera.onvifPort, 1, 0, camera.rtspPort, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
    return true;
}

bool loadCameraIpHint()
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

    camera.valid = true;
    camera.ip = hintedIp;
    camera.onvifPort = ONVIF_PORT_CANDIDATES[0];
    camera.rtspPort = CAMERA_RTSP_PORT;
    camera.onvifPath = DEFAULT_ONVIF_PATH;
    camera.onvifUrl = makeOnvifUrl(camera.ip, camera.onvifPort, camera.onvifPath);

    Serial.printf("Usando CAMERA_IP_HINT: %s\n", camera.ip.toString().c_str());
    PersistentLog::record(PersistentLog::Event::CameraTargetLoad, PersistentLog::Outcome::Success, "ip_hint", camera.ip, camera.onvifPort, 1, 0, camera.rtspPort, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
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

bool candidateAlreadyAdded(const CameraTarget& target)
{
    for (size_t index = 0; index < discoveredCount; index++) {
        if (discovered[index].target.ip == target.ip && discovered[index].target.onvifPort == target.onvifPort && discovered[index].target.onvifPath == target.onvifPath) {
            return true;
        }
    }

    return false;
}

void addDiscoveryResponse(const String& xml)
{
    int searchPosition = 0;

    while (discoveredCount < MAX_DISCOVERED_CAMERAS) {
        String xaddrs;
        int nextXaddrs = 0;

        if (!extractLocalTag(xml, "XAddrs", searchPosition, xaddrs, nextXaddrs)) {
            break;
        }

        String endpointUuid;
        String scopes;
        int ignored = 0;
        extractLocalTag(xml, "Address", searchPosition, endpointUuid, ignored);
        extractLocalTag(xml, "Scopes", searchPosition, scopes, ignored);

        const String firstUrl = firstWhitespaceToken(xaddrs);
        ParsedUrl parsed;

        if (parseHttpUrl(firstUrl, parsed) && !parsed.secure) {
            CameraTarget target;
            target.valid = true;
            target.ip = parsed.ip;
            target.onvifPort = parsed.port;
            target.rtspPort = CAMERA_RTSP_PORT;
            target.onvifPath = parsed.path;
            target.onvifUrl = firstUrl;
            target.endpointUuid = endpointUuid;
            target.scopes = scopes;

            if (!candidateAlreadyAdded(target)) {
                discovered[discoveredCount].target = target;
                discovered[discoveredCount].rawSummary = endpointUuid + " " + scopes + " " + firstUrl;

                discoveredCount++;
            }
        }

        searchPosition = nextXaddrs;
    }
}

int scoreCandidate(const DiscoveryCandidate& candidate)
{
    int score = 0;
    const String combined = candidate.rawSummary;

    if (!camera.endpointUuid.isEmpty() && containsIgnoreCase(combined, camera.endpointUuid)) {
        score += 1000;
    }

    if (camera.valid && candidate.target.ip == camera.ip) {
        score += 500;
    }

    if (!String(CAMERA_MATCH).isEmpty()) {
        if (containsIgnoreCase(combined, CAMERA_MATCH)) {
            score += 2000;
        } else {
            return -1;
        }
    }

    if (containsIgnoreCase(candidate.target.scopes, "onvif")) {
        score += 50;
    }

    return score;
}

bool applyBestDiscoveredCandidate(uint16_t contextId, uint8_t flags)
{
    if (discoveredCount == 0) {
        return false;
    }

    int bestScore = -1;
    int bestIndex = -1;

    for (size_t index = 0; index < discoveredCount; index++) {
        const int score = scoreCandidate(discovered[index]);

        Serial.printf("ONVIF candidato %u: %s | score=%d\n", static_cast<unsigned>(index + 1), discovered[index].target.onvifUrl.c_str(), score);

        if (score > bestScore) {
            bestScore = score;
            bestIndex = static_cast<int>(index);
        }
    }

    if (bestIndex < 0) {
        return false;
    }

    const String oldIp = camera.valid ? camera.ip.toString() : "";
    camera = discovered[bestIndex].target;
    invalidateSnapshotTarget();
    invalidateOnvifServiceCatalog();
    resetSnapshotProfileFailover();
    lastSnapshotResolveAttemptAtMs = 0;
    consecutiveFailures = 0;
    consecutiveSuccesses = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    snapshotTargetFailureStreak = 0;
    saveCameraTarget(contextId, flags);

    const String newIp = camera.ip.toString();

    Serial.printf("Camera selecionada: %s\n", camera.onvifUrl.c_str());

    if (!oldIp.isEmpty() && oldIp != newIp) {
        previousCameraIp = oldIp;
        sendIpChangeAlert(oldIp, newIp);
    }

    return true;
}

bool discoverOnvifCamera(bool manualRequest, uint16_t contextId)
{
    const unsigned long startedAtMs = millis();
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    const uint8_t logFlags = manualRequest ? PersistentLog::FlagManual : PersistentLog::FlagScheduled;
    discoveryRuns++;
    lastRediscoveryAtMs = millis();
    lastEvent = currentTimestamp() + " - Executando ONVIF WS-Discovery";
    Serial.println(lastEvent);
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

    while (millis() - startedAt < DISCOVERY_TIMEOUT_MS) {
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

    const bool selected = applyBestDiscoveredCandidate(logContextId, logFlags);
    lastDiscoveryResult = currentTimestamp() + " - candidatos=" + String(discoveredCount) + "; selecionada=" + (selected ? camera.onvifUrl : String("nenhuma"));

    Serial.println(lastDiscoveryResult);
    PersistentLog::record(PersistentLog::Event::DiscoveryResult, selected ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, selected ? "camera_selected" : "camera_not_found", selected ? camera.ip : IPAddress(), selected ? camera.onvifPort : 0, selected ? 1 : 0, millis() - startedAtMs, static_cast<uint32_t>(discoveredCount), logFlags, selected ? PersistentLog::Level::Info : PersistentLog::Level::Warning, true, logContextId);
    return selected;
}

ServiceProbe probeOnvif(const CameraTarget& target, uint16_t contextId)
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
    const TcpConnectionAttempt connection = connectTcp(client, parsed.ip, parsed.port, TCP_CONNECT_TIMEOUT_MS);

    if (!connection.connected) {
        client.stop();
        result.latencyMs = connection.durationMs;
        PersistentLog::record(PersistentLog::Event::OnvifProbe, PersistentLog::Outcome::Failure, connection.detail, parsed.ip, parsed.port, connection.errorCode, result.latencyMs, TCP_CONNECT_TIMEOUT_MS, PersistentLog::FlagOutbound, PersistentLog::Level::Error, false, logContextId);
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

    while (millis() - responseStartedAt < SERVICE_RESPONSE_TIMEOUT_MS) {
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

    result.protocolResponded = result.statusCode == 401 || result.statusCode == 403 || normalizedHeader.indexOf("content-type: application/soap+xml") >= 0 || normalizedHeader.indexOf("content-type: text/xml") >= 0;

    const char* resultDetail = result.protocolResponded ? "GetSystemDateAndTime" : result.statusCode != 0 ? "non_soap_response" : "invalid_response";

    PersistentLog::record(PersistentLog::Event::OnvifProbe, result.protocolResponded ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, resultDetail, parsed.ip, parsed.port, result.statusCode, result.latencyMs, TCP_CONNECT_TIMEOUT_MS, PersistentLog::FlagOutbound, result.protocolResponded ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, logContextId);

    return result;
}

bool resolveCameraOnvifEndpoint(uint16_t contextId)
{
    if (!camera.valid) {
        return false;
    }

    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    ServiceProbe currentProbe = probeOnvif(camera, logContextId);

    if (currentProbe.protocolResponded) {
        lastOnvifProbe = currentProbe;
        return true;
    }

    for (size_t portIndex = 0; portIndex < ONVIF_PORT_CANDIDATE_COUNT; portIndex++) {
        const uint16_t candidatePort = ONVIF_PORT_CANDIDATES[portIndex];

        if (candidatePort == camera.onvifPort && camera.onvifPath == DEFAULT_ONVIF_PATH) {
            continue;
        }

        CameraTarget candidate = camera;
        candidate.onvifPort = candidatePort;
        candidate.onvifPath = DEFAULT_ONVIF_PATH;
        candidate.onvifUrl = makeOnvifUrl(candidate.ip, candidate.onvifPort, candidate.onvifPath);

        const ServiceProbe candidateProbe = probeOnvif(candidate, logContextId);

        if (!candidateProbe.protocolResponded) {
            continue;
        }

        camera.onvifPort = candidate.onvifPort;
        camera.onvifPath = candidate.onvifPath;
        camera.onvifUrl = candidate.onvifUrl;
        lastOnvifProbe = candidateProbe;
        invalidateSnapshotTarget();
        invalidateOnvifServiceCatalog();
        resetSnapshotProfileFailover();
        lastSnapshotResolveAttemptAtMs = 0;
        saveCameraTarget(logContextId);

        Serial.printf("Servico ONVIF confirmado na porta %u.\n", camera.onvifPort);
        return true;
    }

    lastOnvifProbe = currentProbe;
    return false;
}

bool postOnvifSoap(const String& serviceUrl, const char* action, const String& body, String& responseBody, bool requireResponseBody, int* finalStatusCode, bool* transportFailure, uint32_t timeoutMs, uint16_t contextId)
{
    responseBody = String();
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();

    if (transportFailure != nullptr) {
        *transportFailure = false;
    }

    const String wsseHeader = makeWsseSecurityHeader();
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
    request.setConnectTimeout(min(TCP_CONNECT_TIMEOUT_MS, timeoutMs));
    request.setTimeout(timeoutMs);
    request.setReuse(false);
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
            PersistentLog::record(PersistentLog::Event::OnvifSoap, PersistentLog::Outcome::Failure, logAction, logEndpoint.ip, logEndpoint.port, -1, millis() - attemptStartedAtMs, static_cast<uint32_t>(envelope.length()), static_cast<uint8_t>(PersistentLog::FlagOutbound | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), PersistentLog::Level::Automatic, false, logContextId);
            return -1;
        }

        request.addHeader("Content-Type", contentType);
        applyCameraAuthorization(request, digestAuthorization);
        const int attemptStatusCode = request.POST(envelope);
        const bool requestAccepted = attemptStatusCode >= 200 && attemptStatusCode < 400;
        PersistentLog::record(PersistentLog::Event::OnvifSoap, requestAccepted ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, logAction, logEndpoint.ip, logEndpoint.port, attemptStatusCode, millis() - attemptStartedAtMs, static_cast<uint32_t>(envelope.length()), static_cast<uint8_t>(PersistentLog::FlagOutbound | (authenticationRetry ? PersistentLog::FlagAuthenticationRetry : PersistentLog::FlagNone)), attemptStatusCode == 401 && !authenticationRetry ? PersistentLog::Level::Warning : PersistentLog::Level::Info, false, logContextId);
        return attemptStatusCode;
    };

    int statusCode = executeAttempt(String(), false);

    if (statusCode == 401 && cameraCredentialsConfigured()) {
        DigestChallenge challenge;
        const String authenticateHeader = request.header("WWW-Authenticate");

        if (parseDigestChallenge(authenticateHeader, challenge)) {
            const String digestAuthorization = makeDigestAuthorization("POST", serviceUrl, challenge);

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

void noteCameraClockFailure(const String& result)
{
    cameraClockStatus.checked = true;
    cameraClockStatus.utcValid = false;
    cameraClockStatus.roundTripMs = 0;
    cameraClockStatus.checks++;
    cameraClockStatus.failures++;
    cameraClockStatus.lastCheckAtMs = millis();
    cameraClockStatus.lastResult = result;
}

bool recordCameraClockResponse(const String& responseXml, int64_t referenceUnixTimeMs, uint32_t roundTripMs)
{
    String value;
    int ignored = 0;

    if (extractLocalTag(responseXml, "DateTimeType", 0, value, ignored)) {
        cameraClockStatus.dateTimeType = limitedOnvifText(value, 32);
        onvifMetadata.dateTimeType = cameraClockStatus.dateTimeType;
    }

    if (extractLocalTag(responseXml, "TZ", 0, value, ignored)) {
        cameraClockStatus.timeZone = limitedOnvifText(value, 48);
        onvifMetadata.timeZone = cameraClockStatus.timeZone;
    }

    if (extractLocalTag(responseXml, "DaylightSavings", 0, value, ignored)) {
        value.trim();
        cameraClockStatus.daylightSavings = value == "1" || value.equalsIgnoreCase("true");
    }

    cameraClockStatus.checked = true;
    onvifMetadata.timeLoaded = true;
    cameraClockStatus.checks++;
    cameraClockStatus.lastCheckAtMs = millis();
    cameraClockStatus.roundTripMs = roundTripMs;

    int64_t cameraUnixTimeMs = 0;

    if (referenceUnixTimeMs <= 0 || roundTripMs > CAMERA_CLOCK_MAX_ROUND_TRIP_MS || !parseOnvifUtcDateTime(responseXml, cameraUnixTimeMs)) {
        cameraClockStatus.utcValid = false;
        cameraClockStatus.failures++;
        cameraClockStatus.lastResult = roundTripMs > CAMERA_CLOCK_MAX_ROUND_TRIP_MS ? "Amostra de relogio descartada por latencia ONVIF alta" : "UTCDateTime ONVIF ausente ou invalido";
        return false;
    }

    const int64_t rawDriftMs = cameraUnixTimeMs - referenceUnixTimeMs;

    if (rawDriftMs <= INT32_MIN || rawDriftMs > INT32_MAX) {
        cameraClockStatus.utcValid = false;
        cameraClockStatus.failures++;
        cameraClockStatus.lastResult = "Desvio do relogio fora do intervalo mensuravel";
        return false;
    }

    cameraClockStatus.utcValid = true;
    cameraClockStatus.driftMs = static_cast<int32_t>(rawDriftMs);
    const int32_t absoluteDriftMs = cameraClockStatus.driftMs >= 0 ? cameraClockStatus.driftMs : -cameraClockStatus.driftMs;
    const String direction = cameraClockStatus.driftMs >= 0 ? " adiantada" : " atrasada";

    if (absoluteDriftMs <= CAMERA_CLOCK_HEALTHY_DRIFT_MS) {
        cameraClockStatus.lastResult = "Relogio da camera alinhado ao NTP do ESP32; desvio " + String(cameraClockStatus.driftMs / 1000.0F, 1) + " s";
    } else {
        cameraClockStatus.lastResult = "Camera" + direction + " em " + String(absoluteDriftMs / 1000.0F, 1) + " s frente ao NTP do ESP32";
    }

    return true;
}

void maintainCameraClock()
{
    if (!camera.valid || WiFi.status() != WL_CONNECTED || onvifMetadataStage != OnvifMetadataStage::Complete || !onvifServices.device.available || cameraRebootGraceActive() || healthCheckInProgress) {
        return;
    }

    const unsigned long now = millis();
    const bool clockHealthy = cameraClockStatus.utcValid && cameraClockStatus.driftMs >= -CAMERA_CLOCK_HEALTHY_DRIFT_MS && cameraClockStatus.driftMs <= CAMERA_CLOCK_HEALTHY_DRIFT_MS;
    const unsigned long intervalMs = cameraClockStatus.checked && clockHealthy ? CAMERA_CLOCK_CHECK_INTERVAL_MS : CAMERA_CLOCK_RETRY_INTERVAL_MS;

    if (cameraClockStatus.lastCheckAtMs != 0 && now - cameraClockStatus.lastCheckAtMs < intervalMs) {
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
    const bool succeeded = postOnvifSoap(onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime", "<tds:GetSystemDateAndTime/>", responseXml, true, &statusCode, nullptr, 1000UL);
    const int64_t requestFinishedAtMs = synchronizedUnixTimeMs();
    const SntpReferenceSnapshot referenceAfter = snapshotSntpReference();
    const uint32_t roundTripMs = millis() - requestStartedAtMonotonicMs;

    if (!succeeded || requestStartedAtMs <= 0 || requestFinishedAtMs < requestStartedAtMs) {
        noteCameraClockFailure(String("GetSystemDateAndTime falhou; HTTP ") + statusCode);
        return;
    }

    if (referenceBefore.generation != referenceAfter.generation || !sntpReferenceFresh(referenceAfter)) {
        noteCameraClockFailure("Amostra descartada: referencia SNTP mudou durante a consulta");
        return;
    }

    const int64_t referenceUnixTimeMs = requestStartedAtMs + (requestFinishedAtMs - requestStartedAtMs) / 2LL;
    recordCameraClockResponse(responseXml, referenceUnixTimeMs, roundTripMs);
}

void invalidateOnvifServiceCatalog()
{
    onvifServices = OnvifServiceCatalog{};
    onvifPtz = OnvifPtzStatus{};
    onvifMetadata = OnvifDeviceMetadata{};
    cameraClockStatus = CameraClockStatus{};
    onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
    onvifMetadataStageFailureStreak = 0;
    lastOnvifMetadataRequestAtMs = 0;
    lastOnvifMetadataRequestFailed = false;
    lastBrowserSnapshotDeliveredAtMs = 0;
    lastBrowserSnapshotBytes = 0;
    lastBrowserSnapshotLatencyMs = 0;
    lastHealthEvidenceSource = HealthEvidenceSource::None;
    lastHealthEvidenceAtMs = 0;
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

bool normalizeAdvertisedOnvifUrl(const String& advertisedUrl, String& normalizedUrl)
{
    ParsedHttpResource parsed;

    if (!camera.valid || !parseHttpResourceUrl(advertisedUrl, parsed) || parsed.secure) {
        return false;
    }

    IPAddress advertisedIp;

    if (!advertisedIp.fromString(parsed.host.c_str())) {
        return false;
    }

    const bool unspecifiedAddress = advertisedIp[0] == 0 && advertisedIp[1] == 0 && advertisedIp[2] == 0 && advertisedIp[3] == 0;

    if (!unspecifiedAddress && advertisedIp != camera.ip) {
        return false;
    }

    normalizedUrl = makeOnvifUrl(camera.ip, parsed.port, parsed.path);
    return true;
}

OnvifServiceEndpoint* endpointForOnvifNamespace(const String& serviceNamespace)
{
    if (serviceNamespace == "http://www.onvif.org/ver10/device/wsdl") {
        return &onvifServices.device;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/media/wsdl") {
        return &onvifServices.media1;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/media/wsdl") {
        return &onvifServices.media2;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/events/wsdl") {
        return &onvifServices.events;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/ptz/wsdl") {
        return &onvifServices.ptz;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/imaging/wsdl") {
        return &onvifServices.imaging;
    }

    if (serviceNamespace == "http://www.onvif.org/ver20/analytics/wsdl") {
        return &onvifServices.analytics;
    }

    if (serviceNamespace == "http://www.onvif.org/ver10/deviceIO/wsdl") {
        return &onvifServices.deviceIo;
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

void collectOnvifServiceCapabilities(const String& serviceNamespace, const String& serviceXml)
{
    if (serviceNamespace == "http://www.onvif.org/ver10/device/wsdl") {
        onvifServices.usernameToken = onvifCapabilityIsTrue(serviceXml, "UsernameToken");
        onvifServices.httpDigest = onvifCapabilityIsTrue(serviceXml, "HttpDigest");
    } else if (serviceNamespace == "http://www.onvif.org/ver10/media/wsdl") {
        onvifServices.mediaSnapshotUri = onvifCapabilityIsTrue(serviceXml, "SnapshotUri");
    } else if (serviceNamespace == "http://www.onvif.org/ver10/events/wsdl") {
        onvifServices.eventsPullPoint = onvifCapabilityIsTrue(serviceXml, "WSPullPointSupport");
    } else if (serviceNamespace == "http://www.onvif.org/ver20/imaging/wsdl") {
        onvifServices.imagingStabilizationAdvertised = onvifCapabilityIsTrue(serviceXml, "ImageStabilization");
    } else if (serviceNamespace == "http://www.onvif.org/ver20/analytics/wsdl") {
        onvifServices.analyticsRules = onvifCapabilityIsTrue(serviceXml, "RuleSupport");
        onvifServices.analyticsModules = onvifCapabilityIsTrue(serviceXml, "AnalyticsModuleSupport");
    }
}

bool parseOnvifServices(const String& servicesXml)
{
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
            OnvifServiceEndpoint* endpoint = endpointForOnvifNamespace(serviceNamespace);

            if (endpoint != nullptr && !endpoint->available) {
                String normalizedUrl;

                if (normalizeAdvertisedOnvifUrl(advertisedUrl, normalizedUrl)) {
                    endpoint->available = true;
                    endpoint->url = normalizedUrl;
                    endpoint->majorVersion = onvifUnsignedTag(serviceXml, "Major");
                    endpoint->minorVersion = onvifUnsignedTag(serviceXml, "Minor");
                    onvifServices.serviceCount++;
                    collectOnvifServiceCapabilities(serviceNamespace, serviceXml);
                } else {
                    onvifServices.rejectedEndpoints++;
                }
            }
        }

        if (!hasNextService) {
            break;
        }

        searchPosition = nextServiceStart;
    }

    return onvifServices.serviceCount > 0;
}

bool resolveOnvifServiceCatalog(bool forceRefresh, uint16_t contextId)
{
    if (onvifServices.resolved && !forceRefresh) {
        const bool fallbackRetryDue = onvifServices.usedMediaFallback && millis() - onvifServices.resolvedAtMs >= ONVIF_CATALOG_FALLBACK_RETRY_MS;

        if (!fallbackRetryDue) {
            return onvifServices.media1.available;
        }
    }

    const uint32_t previousAttempts = onvifServices.attempts;
    const uint32_t previousSuccesses = onvifServices.successes;
    const uint32_t previousFailures = onvifServices.failures;
    const bool previousUsedMediaFallback = onvifServices.usedMediaFallback;
    onvifServices = OnvifServiceCatalog{};
    onvifServices.attempts = previousAttempts + 1;
    onvifServices.successes = previousSuccesses;
    onvifServices.failures = previousFailures;
    const unsigned long startedAtMs = millis();
    String responseXml;
    int statusCode = 0;
    bool transportFailure = false;

    const bool received = postOnvifSoap(camera.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/GetServices", "<tds:GetServices><tds:IncludeCapability>true</tds:IncludeCapability></tds:GetServices>", responseXml, true, &statusCode, &transportFailure, SERVICE_RESPONSE_TIMEOUT_MS, contextId);

    onvifServices.lastLatencyMs = millis() - startedAtMs;
    onvifServices.lastStatusCode = statusCode;

    if (received && parseOnvifServices(responseXml)) {
        onvifServices.successes++;
        onvifServices.lastResult = String("GetServices retornou ") + String(onvifServices.serviceCount) + " servicos validos";
    } else {
        onvifServices.failures++;
        onvifServices.lastResult = transportFailure ? "GetServices falhou no transporte; Media1 convencional preservado" : String("GetServices indisponivel; HTTP ") + String(statusCode) + "; Media1 convencional preservado";
    }

    if (!onvifServices.device.available) {
        onvifServices.device.available = true;
        onvifServices.device.url = camera.onvifUrl;
        onvifServices.device.majorVersion = 1;
    }

    if (!onvifServices.media1.available) {
        onvifServices.media1.available = true;
        onvifServices.media1.url = makeOnvifUrl(camera.ip, camera.onvifPort, DEFAULT_ONVIF_MEDIA_PATH);
        onvifServices.media1.majorVersion = 1;
        onvifServices.usedMediaFallback = true;
    }

    onvifServices.resolved = true;
    onvifServices.resolvedAtMs = millis();

    if (previousUsedMediaFallback && !onvifServices.usedMediaFallback) {
        onvifMetadata = OnvifDeviceMetadata{};
        onvifMetadataStage = OnvifMetadataStage::DeviceInformation;
        onvifMetadataStageFailureStreak = 0;
        lastOnvifMetadataRequestAtMs = 0;
        lastOnvifMetadataRequestFailed = false;
    }

    return onvifServices.media1.available;
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

void completeOnvifMetadataRefresh()
{
    onvifMetadataStage = OnvifMetadataStage::Complete;
    onvifMetadata.loadedAtMs = millis();
    onvifMetadataStageFailureStreak = 0;
    lastOnvifMetadataRequestFailed = false;
    onvifMetadata.lastResult = String(onvifMetadata.requests - onvifMetadata.failures) + "/" + String(onvifMetadata.requests) + " consultas ONVIF de leitura concluidas";
}

void advanceOnvifMetadataStage()
{
    switch (onvifMetadataStage) {
        case OnvifMetadataStage::DeviceInformation:
            onvifMetadataStage = OnvifMetadataStage::Scopes;
            break;
        case OnvifMetadataStage::Scopes:
            onvifMetadataStage = OnvifMetadataStage::SystemTime;
            break;
        case OnvifMetadataStage::SystemTime:
            onvifMetadataStage = OnvifMetadataStage::EventProperties;
            break;
        case OnvifMetadataStage::EventProperties:
            completeOnvifMetadataRefresh();
            break;
        case OnvifMetadataStage::Complete:
            break;
    }
}

void noteOnvifMetadataFailure(const char* operation, int statusCode)
{
    onvifMetadata.failures++;

    if (onvifMetadataStageFailureStreak < 255) {
        onvifMetadataStageFailureStreak++;
    }

    const bool permanentStatus = (statusCode >= 400 && statusCode <= 499 && statusCode != 408 && statusCode != 429) || statusCode == 501;
    const bool retryLimitReached = onvifMetadataStageFailureStreak >= 3;

    if (permanentStatus || retryLimitReached) {
        onvifMetadata.lastResult = String(operation) + " nao suportado ou indisponivel; etapa ignorada";
        onvifMetadataStageFailureStreak = 0;
        lastOnvifMetadataRequestFailed = false;
        advanceOnvifMetadataStage();
        return;
    }

    lastOnvifMetadataRequestFailed = true;
    onvifMetadata.lastResult = String(operation) + " falhou; HTTP " + String(statusCode) + "; nova tentativa em 30 s";
}

void refreshOnvifDeviceMetadata()
{
    if (!camera.valid || WiFi.status() != WL_CONNECTED) {
        return;
    }

    const unsigned long now = millis();

    if (onvifMetadataStage == OnvifMetadataStage::Complete) {
        const bool fallbackRetryDue = onvifServices.usedMediaFallback && now - onvifServices.resolvedAtMs >= ONVIF_CATALOG_FALLBACK_RETRY_MS;

        if (!fallbackRetryDue) {
            return;
        }

        CameraIoGuard cameraIoGuard;

        if (cameraIoGuard.acquired()) {
            resolveOnvifServiceCatalog();
        }

        return;
    }

    const unsigned long retryIntervalMs = lastOnvifMetadataRequestFailed ? 30000UL : 250UL;

    if (lastOnvifMetadataRequestAtMs != 0 && now - lastOnvifMetadataRequestAtMs < retryIntervalMs) {
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        return;
    }

    if (!resolveOnvifServiceCatalog()) {
        lastOnvifMetadataRequestAtMs = now;
        lastOnvifMetadataRequestFailed = true;
        onvifMetadata.lastResult = "Catalogo ONVIF indisponivel; nova tentativa em 30 s";
        return;
    }

    String responseXml;
    int statusCode = 0;
    bool succeeded = false;
    onvifMetadata.requests++;
    lastOnvifMetadataRequestAtMs = millis();

    switch (onvifMetadataStage) {
        case OnvifMetadataStage::DeviceInformation: {
            succeeded = postOnvifSoap(onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation", "<tds:GetDeviceInformation/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure("GetDeviceInformation", statusCode);
                return;
            }

            String value;
            int ignored = 0;

            if (extractLocalTag(responseXml, "Manufacturer", 0, value, ignored)) {
                onvifMetadata.manufacturer = limitedOnvifText(value);
            }

            if (extractLocalTag(responseXml, "Model", 0, value, ignored)) {
                onvifMetadata.model = limitedOnvifText(value);
            }

            if (extractLocalTag(responseXml, "FirmwareVersion", 0, value, ignored)) {
                onvifMetadata.firmware = limitedOnvifText(value);
            }

            onvifMetadata.loaded = true;
            onvifMetadataStage = OnvifMetadataStage::Scopes;
            break;
        }

        case OnvifMetadataStage::Scopes:
            succeeded = postOnvifSoap(onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetScopes", "<tds:GetScopes/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure("GetScopes", statusCode);
                return;
            }

            onvifMetadata.scopeCount = countOnvifOpeningTags(responseXml, "Scopes");
            onvifMetadata.profileT = responseXml.indexOf("Profile/T") >= 0 || responseXml.indexOf("Profile%2FT") >= 0;
            onvifMetadata.scopesLoaded = true;
            onvifMetadataStage = OnvifMetadataStage::SystemTime;
            break;

        case OnvifMetadataStage::SystemTime: {
            succeeded = postOnvifSoap(onvifServices.device.url, "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime", "<tds:GetSystemDateAndTime/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure("GetSystemDateAndTime", statusCode);
                return;
            }

            String value;
            int ignored = 0;

            if (extractLocalTag(responseXml, "DateTimeType", 0, value, ignored)) {
                onvifMetadata.dateTimeType = limitedOnvifText(value, 32);
            }

            if (extractLocalTag(responseXml, "TZ", 0, value, ignored)) {
                onvifMetadata.timeZone = limitedOnvifText(value, 48);
            }

            onvifMetadata.timeLoaded = true;
            onvifMetadataStage = OnvifMetadataStage::EventProperties;
            break;
        }

        case OnvifMetadataStage::EventProperties:
            if (!onvifServices.events.available) {
                onvifMetadata.requests--;
                completeOnvifMetadataRefresh();
                return;
            }

            succeeded = postOnvifSoap(onvifServices.events.url, "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesRequest", "<tev:GetEventProperties/>", responseXml, true, &statusCode, nullptr, 1000UL);

            if (!succeeded) {
                noteOnvifMetadataFailure("GetEventProperties", statusCode);
                return;
            }

            onvifMetadata.eventTopicCount = countOnvifTopicMarkers(responseXml);
            onvifMetadata.eventDescriptionCount = countOnvifOpeningTags(responseXml, "MessageDescription");
            onvifMetadata.motionEvents = responseXml.indexOf("Motion") >= 0;
            onvifMetadata.tamperEvents = responseXml.indexOf("Tamper") >= 0;
            onvifMetadata.signalLossEvents = responseXml.indexOf("SignalLoss") >= 0;
            onvifMetadata.imageQualityEvents = responseXml.indexOf("ImageTooBlurry") >= 0 || responseXml.indexOf("ImageTooDark") >= 0 || responseXml.indexOf("ImageTooBright") >= 0;
            onvifMetadata.eventPropertiesLoaded = true;
            completeOnvifMetadataRefresh();
            return;

        case OnvifMetadataStage::Complete:
            return;
    }

    onvifMetadataStageFailureStreak = 0;
    lastOnvifMetadataRequestFailed = false;
    onvifMetadata.lastResult = "Inventario ONVIF em andamento";
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

bool selectOnvifProfiles(const String& profilesXml, String& selectedToken, String& selectedName, String& selectedPtzToken, String& selectedPtzName)
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

        if (!snapshotProfileTokenToSkip.isEmpty() && token == snapshotProfileTokenToSkip) {
            if (!skipped.valid) {
                skipped = candidate;
            }
            continue;
        }

        const bool jpegProfile = encoding == "JPEG" || encoding == "MJPEG";
        const bool nameMatches = !String(CAMERA_SNAPSHOT_PROFILE_MATCH).isEmpty() && containsIgnoreCase(name, CAMERA_SNAPSHOT_PROFILE_MATCH);

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

bool resolveSnapshotTarget(uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    lastSnapshotResolveAttemptAtMs = millis();
    lastSnapshotResolveStatusCode = 0;
    lastSnapshotResolveTransportFailure = false;
    invalidateSnapshotTarget();

    if (!camera.valid || WiFi.status() != WL_CONNECTED) {
        lastSnapshotResult = "Camera ou Wi-Fi indisponivel";
        return false;
    }

    if (!lastOnvifProbe.protocolResponded && !resolveCameraOnvifEndpoint(logContextId)) {
        lastSnapshotResolveStatusCode = lastOnvifProbe.statusCode;
        lastSnapshotResult = "Servico ONVIF nao localizado";
        return false;
    }

    lastSnapshotResolveAttemptAtMs = millis();

    if (!resolveOnvifServiceCatalog(false, logContextId)) {
        lastSnapshotResult = "Catalogo ONVIF sem servico Media1";
        return false;
    }

    const String mediaUrl = onvifServices.media1.url;

    String profileToken;
    String profileName;
    String ptzProfileToken;
    String ptzProfileName;

    {
        String profilesXml;
        int profilesStatusCode = 0;
        bool profilesTransportFailure = false;

        if (!postOnvifSoap(mediaUrl, "http://www.onvif.org/ver10/media/wsdl/GetProfiles", "<trt:GetProfiles/>", profilesXml, true, &profilesStatusCode, &profilesTransportFailure, SERVICE_RESPONSE_TIMEOUT_MS, logContextId)) {
            lastSnapshotResolveStatusCode = profilesStatusCode;
            lastSnapshotResolveTransportFailure = profilesTransportFailure;
            lastSnapshotResult = "ONVIF nao retornou os perfis de midia";
            return false;
        }

        lastSnapshotResolveStatusCode = profilesStatusCode;
        if (!selectOnvifProfiles(profilesXml, profileToken, profileName, ptzProfileToken, ptzProfileName)) {
            lastSnapshotResult = "Nenhum perfil ONVIF compativel";
            return false;
        }

        if (profileToken == snapshotProfileTokenToSkip) {
            resetSnapshotProfileFailover();
        }

        if (onvifServices.ptz.available && !ptzProfileToken.isEmpty()) {
            onvifPtz.profileResolved = true;
            onvifPtz.profileToken = ptzProfileToken;
            onvifPtz.profileName = ptzProfileName;
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

    if (!postOnvifSoap(mediaUrl, "http://www.onvif.org/ver10/media/wsdl/GetSnapshotUri", snapshotRequest, snapshotXml, true, &snapshotStatusCode, &snapshotTransportFailure, SERVICE_RESPONSE_TIMEOUT_MS, logContextId)) {
        lastSnapshotResolveStatusCode = snapshotStatusCode;
        lastSnapshotResolveTransportFailure = snapshotTransportFailure;
        lastSnapshotResult = "ONVIF nao retornou o snapshot";
        return false;
    }

    lastSnapshotResolveStatusCode = snapshotStatusCode;

    String snapshotUrl;
    int ignoredPosition = 0;

    if (!extractLocalTag(snapshotXml, "Uri", 0, snapshotUrl, ignoredPosition)) {
        lastSnapshotResult = "Resposta ONVIF sem URI de snapshot";
        return false;
    }

    snapshotUrl = xmlUnescape(snapshotUrl);
    snapshotUrl.trim();
    ParsedHttpResource parsedSnapshot;

    if (!parseHttpResourceUrl(snapshotUrl, parsedSnapshot)) {
        lastSnapshotResult = "Camera retornou um URI de snapshot invalido";
        return false;
    }

    if (parsedSnapshot.secure) {
        lastSnapshotResult = "Snapshot HTTPS local ainda nao suportado";
        return false;
    }

    snapshotTarget.valid = true;
    snapshotTarget.url = makeOnvifUrl(camera.ip, parsedSnapshot.port, parsedSnapshot.path);
    snapshotTarget.profileToken = profileToken;
    snapshotTarget.profileName = profileName;
    snapshotTargetFailureStreak = 0;
    lastSnapshotResult = currentTimestamp() + " - perfil " + snapshotTarget.profileName;

    return true;
}

int onvifPtzPresetIndex(const String& token)
{
    for (uint16_t index = 0; index < onvifPtz.presetCount; index++) {
        if (onvifPtz.presets[index].token == token) {
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

bool resolveOnvifPtzProfile(uint16_t contextId = 0)
{
    if (onvifPtz.profileResolved && !onvifPtz.profileToken.isEmpty()) {
        return true;
    }

    if (!onvifServices.media1.available || !onvifServices.ptz.available) {
        onvifPtz.lastResult = "Servicos Media1 ou PTZ indisponiveis";
        return false;
    }

    String profilesXml;
    int statusCode = 0;

    if (!postOnvifSoap(onvifServices.media1.url, "http://www.onvif.org/ver10/media/wsdl/GetProfiles", "<trt:GetProfiles/>", profilesXml, true, &statusCode, nullptr, SERVICE_RESPONSE_TIMEOUT_MS, contextId)) {
        onvifPtz.lastRefreshStatusCode = statusCode;
        onvifPtz.lastResult = String("GetProfiles para PTZ falhou; HTTP ") + statusCode;
        return false;
    }

    String ignoredSnapshotToken;
    String ignoredSnapshotName;
    String profileToken;
    String profileName;
    selectOnvifProfiles(profilesXml, ignoredSnapshotToken, ignoredSnapshotName, profileToken, profileName);

    if (profileToken.isEmpty()) {
        onvifPtz.lastResult = "Nenhum perfil de midia possui configuracao PTZ";
        return false;
    }

    onvifPtz.profileResolved = true;
    onvifPtz.profileToken = profileToken;
    onvifPtz.profileName = profileName;
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

bool refreshOnvifPtzPresets(uint16_t contextId = 0)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    onvifPtz.lastRefreshAttemptAtMs = millis();
    onvifPtz.refreshAttempts++;
    const unsigned long startedAtMs = millis();

    if (!resolveOnvifPtzProfile(logContextId)) {
        onvifPtz.refreshFailures++;
        onvifPtz.lastRefreshLatencyMs = millis() - startedAtMs;
        return false;
    }

    String requestBody;
    requestBody.reserve(onvifPtz.profileToken.length() + 112);
    requestBody += "<tptz:GetPresets><tptz:ProfileToken>";
    requestBody += htmlEscape(onvifPtz.profileToken);
    requestBody += "</tptz:ProfileToken></tptz:GetPresets>";

    String responseXml;
    int statusCode = 0;
    bool transportFailure = false;
    const bool received = postOnvifSoap(onvifServices.ptz.url, "http://www.onvif.org/ver20/ptz/wsdl/GetPresets", requestBody, responseXml, true, &statusCode, &transportFailure, SERVICE_RESPONSE_TIMEOUT_MS, logContextId);
    onvifPtz.lastRefreshLatencyMs = millis() - startedAtMs;
    onvifPtz.lastRefreshStatusCode = statusCode;

    std::unique_ptr<OnvifPtzPreset[]> parsedPresets(new (std::nothrow) OnvifPtzPreset[ONVIF_PTZ_PRESET_CAPACITY]);
    uint16_t parsedCount = 0;
    uint16_t reportedCount = 0;
    bool truncated = false;

    if (!parsedPresets) {
        onvifPtz.refreshFailures++;
        onvifPtz.lastResult = "GetPresets sem memoria para processar o catalogo";
        return false;
    }

    if (!received || !parseOnvifPtzPresets(responseXml, parsedPresets.get(), parsedCount, reportedCount, truncated)) {
        onvifPtz.refreshFailures++;
        onvifPtz.lastResult = transportFailure ? "GetPresets falhou no transporte" : String("GetPresets falhou; HTTP ") + statusCode;
        return false;
    }

    for (size_t index = 0; index < ONVIF_PTZ_PRESET_CAPACITY; index++) {
        onvifPtz.presets[index] = OnvifPtzPreset{};
    }

    for (uint16_t index = 0; index < parsedCount; index++) {
        onvifPtz.presets[index] = parsedPresets[index];
    }

    onvifPtz.presetsLoaded = true;
    onvifPtz.truncated = truncated;
    onvifPtz.presetCount = parsedCount;
    onvifPtz.reportedPresetCount = reportedCount;
    onvifPtz.refreshSuccesses++;
    onvifPtz.refreshedAtMs = millis();
    onvifPtz.lastResult = currentTimestamp() + " - GetPresets retornou " + String(reportedCount) + " posicoes";

    if (truncated) {
        onvifPtz.lastResult += "; exibindo as primeiras ";
        onvifPtz.lastResult += ONVIF_PTZ_PRESET_CAPACITY;
    }

    Serial.println(onvifPtz.lastResult);
    return true;
}

void maintainOnvifPtzPresets()
{
    if (!camera.valid || WiFi.status() != WL_CONNECTED || !onvifServices.resolved || !onvifServices.ptz.available || cameraRebootGraceActive() || healthCheckInProgress) {
        return;
    }

    const unsigned long intervalMs = onvifPtz.presetsLoaded ? ONVIF_PTZ_PRESET_REFRESH_INTERVAL_MS : ONVIF_PTZ_PRESET_RETRY_INTERVAL_MS;

    if (onvifPtz.lastRefreshAttemptAtMs != 0 && millis() - onvifPtz.lastRefreshAttemptAtMs < intervalMs) {
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        return;
    }

    refreshOnvifPtzPresets();
}

bool requestOnvifPtzPreset(const String& presetToken, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        onvifPtz.lastCommandResult = currentTimestamp() + " - canal da camera ocupado";
        return false;
    }

    if (!camera.valid || !onvifServices.ptz.available || cameraRebootGraceActive()) {
        onvifPtz.lastCommandResult = currentTimestamp() + " - servico PTZ indisponivel";
        return false;
    }

    if ((!onvifPtz.presetsLoaded || !onvifPtz.profileResolved) && !refreshOnvifPtzPresets(logContextId)) {
        onvifPtz.lastCommandResult = currentTimestamp() + " - presets PTZ indisponiveis";
        return false;
    }

    const int presetIndex = onvifPtzPresetIndex(presetToken);

    if (presetIndex < 0) {
        onvifPtz.lastCommandResult = currentTimestamp() + " - preset nao encontrado";
        return false;
    }

    const OnvifPtzPreset& preset = onvifPtz.presets[presetIndex];
    String requestBody;
    requestBody.reserve(onvifPtz.profileToken.length() + preset.token.length() + 176);
    requestBody += "<tptz:GotoPreset><tptz:ProfileToken>";
    requestBody += htmlEscape(onvifPtz.profileToken);
    requestBody += "</tptz:ProfileToken><tptz:PresetToken>";
    requestBody += htmlEscape(preset.token);
    requestBody += "</tptz:PresetToken></tptz:GotoPreset>";

    onvifPtz.commandAttempts++;
    onvifPtz.lastCommandAtMs = millis();
    onvifPtz.lastCommandToken = preset.token;
    const unsigned long startedAtMs = millis();
    String responseXml;
    int statusCode = 0;
    const bool accepted = postOnvifSoap(onvifServices.ptz.url, "http://www.onvif.org/ver20/ptz/wsdl/GotoPreset", requestBody, responseXml, false, &statusCode, nullptr, SERVICE_RESPONSE_TIMEOUT_MS, logContextId);
    onvifPtz.lastCommandLatencyMs = millis() - startedAtMs;
    onvifPtz.lastCommandStatusCode = statusCode;

    if (accepted) {
        onvifPtz.commandAccepted++;
        lastBrowserSnapshotDeliveredAtMs = 0;
        onvifPtz.lastCommandResult = currentTimestamp() + " - comando para " + onvifPtzPresetLabel(preset) + " aceito pela câmera";
    } else {
        onvifPtz.commandErrors++;
        onvifPtz.lastCommandResult = currentTimestamp() + " - comando para " + onvifPtzPresetLabel(preset) + " rejeitado; HTTP " + statusCode;
    }

    Serial.println(onvifPtz.lastCommandResult);
    PersistentLog::record(PersistentLog::Event::PtzCommand, accepted ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, preset.token.c_str(), camera.ip, camera.onvifPort, statusCode, onvifPtz.lastCommandLatencyMs, static_cast<uint32_t>(presetIndex), PersistentLog::FlagManual, accepted ? PersistentLog::Level::Info : PersistentLog::Level::Error, true, logContextId);
    return accepted;
}

void noteSnapshotRequestFailure(int statusCode)
{
    if (snapshotTargetFailureStreak < 255) {
        snapshotTargetFailureStreak++;
    }

    const bool permanentUriFailure = statusCode == 404 || statusCode == 410;
    const bool authenticationFailure = statusCode == 401 || statusCode == 403;
    const bool testingAlternateProfile = snapshotProfileFailoverPending && !snapshotProfileTokenToSkip.isEmpty() && snapshotTarget.profileToken != snapshotProfileTokenToSkip;

    if (testingAlternateProfile) {
        if (permanentUriFailure || snapshotTargetFailureStreak >= SNAPSHOT_FAILURES_BEFORE_RERESOLVE) {
            resetSnapshotProfileFailover();
        }

        return;
    }

    if (permanentUriFailure || (!authenticationFailure && snapshotTargetFailureStreak >= SNAPSHOT_FAILURES_BEFORE_RERESOLVE)) {
        snapshotProfileTokenToSkip = snapshotTarget.profileToken;
        snapshotProfileFailoverPending = true;
        invalidateSnapshotTarget();
        lastSnapshotResolveAttemptAtMs = 0;
        snapshotTargetFailureStreak = 0;
    }
}

bool probeCameraSnapshotJpeg(uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    SnapshotProbe result;
    result.attempted = true;
    snapshotProbeRuns++;
    const unsigned long startedAt = millis();
    bool resolutionAttempted = false;

    if (!snapshotTarget.valid) {
        if (lastSnapshotResolveAttemptAtMs != 0 && millis() - lastSnapshotResolveAttemptAtMs < SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS) {
            result.detail = "Aguardando nova tentativa de resolver o snapshot";
        } else {
            resolutionAttempted = true;

            if (!resolveSnapshotTarget(logContextId)) {
                result.detail = lastSnapshotResult;
            }
        }
    }

    result.targetResolved = snapshotTarget.valid;

    if (!snapshotTarget.valid) {
        result.statusCode = lastSnapshotResolveStatusCode;
        result.httpResponded = result.statusCode >= 100;
        result.requestAttempted = resolutionAttempted;
        result.resolutionAttempted = resolutionAttempted;
        result.resolutionTransportFailure = resolutionAttempted && lastSnapshotResolveTransportFailure;
        result.conclusive = resolutionAttempted && result.statusCode >= 500 && result.statusCode <= 599;
        result.latencyMs = millis() - startedAt;

        if (resolutionAttempted) {
            snapshotProbeFailures++;
        }

        if (result.conclusive) {
            resetSnapshotProfileFailover();
        }

        lastSnapshotProbe = result;
        return false;
    }

    result.requestAttempted = true;
    result.conclusive = true;
    HTTPClient& request = cameraSnapshotHttpSession.request;
    const int statusCode = openAuthenticatedCameraGet(snapshotTarget.url, logContextId);

    result.statusCode = statusCode;
    result.latencyMs = millis() - startedAt;
    result.httpResponded = statusCode >= 100;

    if (statusCode < 200 || statusCode >= 300) {
        result.detail = statusCode < 0 ? String("Falha HTTP: ") + HTTPClient::errorToString(statusCode) : String("Camera retornou HTTP ") + String(statusCode);
        finishCameraSnapshotGet(false);
        noteSnapshotRequestFailure(statusCode);
        snapshotProbeFailures++;
        lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        lastSnapshotProbe = result;
        return false;
    }

    const int contentLength = request.getSize();

    if (contentLength == 0 || (contentLength > 0 && contentLength < static_cast<int>(MIN_VALID_SNAPSHOT_SIZE_BYTES)) || contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)) {
        result.detail = "Tamanho de snapshot invalido";
        finishCameraSnapshotGet(false);
        noteSnapshotRequestFailure(statusCode);
        snapshotProbeFailures++;
        lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        lastSnapshotProbe = result;
        return false;
    }

    WiFiClient* source = request.getStreamPtr();

    if (source == nullptr) {
        result.detail = "Fluxo HTTP do snapshot indisponivel";
        finishCameraSnapshotGet(false);
        noteSnapshotRequestFailure(statusCode);
        snapshotProbeFailures++;
        lastSnapshotResult = currentTimestamp() + " - " + result.detail;
        lastSnapshotProbe = result;
        return false;
    }

    uint8_t buffer[SNAPSHOT_PROBE_BUFFER_SIZE];
    unsigned long lastDataAtMs = millis();
    uint8_t previousByte = 0;
    uint8_t firstByte = 0;
    uint8_t secondByte = 0;
    bool jpegStarted = false;
    size_t jpegEndOffset = 0;

    while (result.bytesRead < MAX_SNAPSHOT_SIZE_BYTES) {
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

        if (!source->connected() || millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS) {
            break;
        }

        delay(1);
    }

    result.latencyMs = millis() - startedAt;

    const bool completeLength = contentLength < 0 || result.bytesRead == static_cast<size_t>(contentLength);
    const bool jpegEndNearTail = jpegEndOffset > 0 && result.bytesRead >= jpegEndOffset && result.bytesRead - jpegEndOffset <= 16;

    result.jpegValid = jpegStarted && jpegEndNearTail && completeLength && result.bytesRead >= MIN_VALID_SNAPSHOT_SIZE_BYTES;
    finishCameraSnapshotGet(result.jpegValid && contentLength > 0 && result.bytesRead == static_cast<size_t>(contentLength));

    if (result.jpegValid) {
        snapshotTargetFailureStreak = 0;
        resetSnapshotProfileFailover();
        result.detail = String("JPEG valido; ") + String(result.bytesRead) + " bytes";
        lastSnapshotResult = currentTimestamp() + " - " + result.detail + "; perfil " + snapshotTarget.profileName;
        lastSnapshotProbe = result;
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

    noteSnapshotRequestFailure(statusCode);
    snapshotProbeFailures++;
    lastSnapshotResult = currentTimestamp() + " - " + result.detail;
    lastSnapshotProbe = result;
    return false;
}

bool snapshotFailureAllowsAutomaticReboot()
{
    if (!lastSnapshotProbe.requestAttempted || !lastSnapshotProbe.conclusive || snapshotProfileFailoverPending) {
        return false;
    }

    if ((lastSnapshotProbe.statusCode >= 400 && lastSnapshotProbe.statusCode <= 499) || lastSnapshotProbe.statusCode == 501) {
        return false;
    }

    return !containsIgnoreCase(lastSnapshotProbe.detail, "nenhum perfil") && !containsIgnoreCase(lastSnapshotProbe.detail, "uri de snapshot invalido") && !containsIgnoreCase(lastSnapshotProbe.detail, "https local ainda nao suportado");
}

void recordCameraRebootEvidence(bool manualRequest)
{
    CameraRebootEvidence& evidence = cameraRebootHistory[cameraRebootHistoryNext];
    evidence = CameraRebootEvidence{};
    copySettingText(evidence.timestamp, currentTimestamp());
    copySettingText(evidence.snapshotProfile, snapshotTarget.profileName);
    evidence.uptimeSeconds = millis() / 1000UL;
    evidence.snapshotStatus = static_cast<int16_t>(lastSnapshotProbe.statusCode);
    evidence.snapshotLatencyMs = lastSnapshotProbe.latencyMs;
    evidence.snapshotBytes = static_cast<uint32_t>(lastSnapshotProbe.bytesRead);
    evidence.consecutiveImageFailures = consecutiveFailures;
    evidence.esp32WifiRssi = static_cast<int16_t>(WiFi.RSSI());
    evidence.freeHeapBytes = ESP.getFreeHeap();
    evidence.manual = manualRequest;

    cameraRebootHistoryNext = (cameraRebootHistoryNext + 1U) % CAMERA_REBOOT_HISTORY_CAPACITY;
    if (cameraRebootHistoryCount < CAMERA_REBOOT_HISTORY_CAPACITY) {
        cameraRebootHistoryCount++;
    }
}

bool requestCameraRebootOnvif(bool manualRequest, uint16_t contextId)
{
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    const unsigned long rebootStartedAtMs = millis();
    const auto recordReboot = [&](PersistentLog::Outcome outcome, const char* detail, int statusCode, PersistentLog::Level level, bool critical) {
        PersistentLog::record(PersistentLog::Event::CameraReboot, outcome, detail, camera.ip, camera.onvifPort, statusCode, millis() - rebootStartedAtMs, cameraRebootAttempts, manualRequest ? PersistentLog::FlagManual : PersistentLog::FlagScheduled, level, critical, logContextId);
    };
    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        lastCameraRebootResult = currentTimestamp() + " - canal da camera ocupado";
        recordReboot(PersistentLog::Outcome::Skipped, "camera_io_busy", 0, PersistentLog::Level::Warning, false);
        return false;
    }

    if (!camera.valid) {
        lastCameraRebootResult = currentTimestamp() + " - camera ainda nao localizada";
        recordReboot(PersistentLog::Outcome::Skipped, "camera_not_found", 0, PersistentLog::Level::Warning, false);
        return false;
    }

    if (!manualRequest && !ENABLE_ONVIF_AUTO_REBOOT) {
        lastCameraRebootResult = currentTimestamp() + " - reboot automatico desativado";
        recordReboot(PersistentLog::Outcome::Skipped, "automatic_disabled", 0, PersistentLog::Level::Info, false);
        return false;
    }

    refreshCameraRebootWindow();
    const unsigned long now = millis();

    if (!manualRequest && now - bootAtMs < STARTUP_GRACE_MS) {
        const unsigned long remainingMs = STARTUP_GRACE_MS - (now - bootAtMs);
        lastCameraRebootResult = currentTimestamp() + " - reboot aguardando a tolerancia inicial; faltam " + durationText(remainingMs);
        recordReboot(PersistentLog::Outcome::Skipped, "startup_grace", 0, PersistentLog::Level::Info, false);
        return false;
    }

    if (cameraRebootGraceActive()) {
        recordReboot(PersistentLog::Outcome::Skipped, "reboot_grace", 0, PersistentLog::Level::Info, false);
        return false;
    }

    if (lastCameraRebootAttemptAtMs != 0 && now - lastCameraRebootAttemptAtMs < ONVIF_REBOOT_COOLDOWN_MS) {
        const unsigned long remainingMs = ONVIF_REBOOT_COOLDOWN_MS - (now - lastCameraRebootAttemptAtMs);
        lastCameraRebootResult = currentTimestamp() + " - novo reboot bloqueado por cooldown; faltam " + durationText(remainingMs);
        recordReboot(PersistentLog::Outcome::Skipped, "cooldown", 0, PersistentLog::Level::Warning, false);
        return false;
    }

    if (cameraRebootAttemptsInWindow >= MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW) {
        const unsigned long elapsedMs = now - cameraRebootWindowStartedAtMs;
        const unsigned long remainingMs = elapsedMs < ONVIF_REBOOT_WINDOW_MS ? ONVIF_REBOOT_WINDOW_MS - elapsedMs : 0;
        lastCameraRebootResult = currentTimestamp() + " - limite de tentativas aceitas atingido; nova janela em " + durationText(remainingMs);
        recordReboot(PersistentLog::Outcome::Skipped, "window_limit", 0, PersistentLog::Level::Warning, true);
        return false;
    }

    lastCameraRebootAttemptAtMs = now;
    cameraRebootAttempts++;

    if (manualRequest) {
        manualCameraRebootAttempts++;
    } else {
        automaticCameraRebootAttempts++;
    }

    String responseBody;
    int statusCode = 0;
    const bool accepted = postOnvifSoap(camera.onvifUrl, "http://www.onvif.org/ver10/device/wsdl/SystemReboot", "<tds:SystemReboot/>", responseBody, false, &statusCode, nullptr, SERVICE_RESPONSE_TIMEOUT_MS, logContextId);

    if (!accepted) {
        cameraRebootErrors++;

        if (manualRequest) {
            manualCameraRebootErrors++;
        } else {
            automaticCameraRebootErrors++;
        }

        lastCameraRebootResult = currentTimestamp() + " - ";
        lastCameraRebootResult += manualRequest ? "manual" : "automatico";
        lastCameraRebootResult += " rejeitado; ";

        if (statusCode < 0) {
            lastCameraRebootResult += HTTPClient::errorToString(statusCode);
        } else {
            lastCameraRebootResult += "HTTP ";
            lastCameraRebootResult += statusCode;
        }

        String faultText;
        int ignoredPosition = 0;
        extractLocalTag(responseBody, "Text", 0, faultText, ignoredPosition);

        if (!faultText.isEmpty()) {
            lastCameraRebootResult += "; ";
            lastCameraRebootResult += faultText;
        }

        if (statusCode == 401 && !cameraCredentialsConfigured()) {
            lastCameraRebootResult += "; configure CAMERA_ONVIF_USERNAME_VALUE e CAMERA_ONVIF_PASSWORD_VALUE";
        }

        Serial.println(lastCameraRebootResult);
        recordReboot(PersistentLog::Outcome::Failure, "SystemReboot", statusCode, PersistentLog::Level::Error, true);
        return false;
    }

    cameraRebootAccepted++;

    if (manualRequest) {
        manualCameraRebootAccepted++;
    } else {
        automaticCameraRebootAccepted++;
    }

    lastCameraRebootEvidence = currentTimestamp() + " - origem ";
    lastCameraRebootEvidence += manualRequest ? "manual" : "automatica";
    lastCameraRebootEvidence += "; JPEG HTTP ";
    lastCameraRebootEvidence += lastSnapshotProbe.statusCode;
    lastCameraRebootEvidence += ", ";
    lastCameraRebootEvidence += lastSnapshotProbe.bytesRead;
    lastCameraRebootEvidence += " bytes em ";
    lastCameraRebootEvidence += lastSnapshotProbe.latencyMs;
    lastCameraRebootEvidence += " ms; falhas JPEG ";
    lastCameraRebootEvidence += consecutiveFailures;
    lastCameraRebootEvidence += "; Wi-Fi ESP32 ";
    lastCameraRebootEvidence += WiFi.RSSI();
    lastCameraRebootEvidence += " dBm; heap livre ";
    lastCameraRebootEvidence += ESP.getFreeHeap();
    lastCameraRebootEvidence += " bytes";
    recordCameraRebootEvidence(manualRequest);

    const unsigned long acceptedAtMs = millis();
    cameraRebootAcceptedAtMs[cameraRebootAttemptsInWindow++] = acceptedAtMs;
    cameraRebootWindowStartedAtMs = cameraRebootAcceptedAtMs[0];
    cameraRebootGraceStartedAtMs = acceptedAtMs;
    invalidateSnapshotTarget();
    invalidateOnvifServiceCatalog();
    resetSnapshotProfileFailover();
    lastSnapshotResolveAttemptAtMs = now;
    snapshotTargetFailureStreak = 0;
    consecutiveFailures = 0;
    consecutiveSuccesses = 0;
    consecutiveControlFailures = 0;
    consecutiveInconclusiveChecks = 0;
    consecutiveMediaServiceFailures = 0;
    lastSnapshotResolveStatusCode = 0;
    lastSnapshotResolveTransportFailure = false;
    lastSnapshotProbe = SnapshotProbe{};
    lastSnapshotProbe.attempted = true;
    lastSnapshotProbe.detail = "Aguardando a camera reiniciar";

    String rebootMessage;
    int ignoredPosition = 0;
    extractLocalTag(responseBody, "Message", 0, rebootMessage, ignoredPosition);

    lastCameraRebootResult = currentTimestamp() + " - ";
    lastCameraRebootResult += manualRequest ? "manual" : "automatico";
    lastCameraRebootResult += " comando aceito; reinicio ainda nao confirmado; HTTP ";
    lastCameraRebootResult += statusCode;

    if (!rebootMessage.isEmpty()) {
        lastCameraRebootResult += "; ";
        lastCameraRebootResult += rebootMessage;
    }

    setState(CameraState::Recovering, "Reinicializacao ONVIF solicitada; aguardando a camera");
    Serial.println(lastCameraRebootResult);
    recordReboot(PersistentLog::Outcome::Success, "SystemReboot", statusCode, PersistentLog::Level::Info, true);
    return true;
}

void sendSnapshotError(int statusCode, const char* message, bool countError = true)
{
    if (countError) {
        snapshotErrors++;
    }

    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(statusCode, "application/json; charset=utf-8", String("{\"error\":\"") + message + "\"}");
}

void serveCameraSnapshot()
{
    if (healthCheckInProgress) {
        previewBusyResponses++;
        sendSnapshotError(503, "health_check_in_progress", false);
        return;
    }

    CameraIoGuard cameraIoGuard;

    if (!cameraIoGuard.acquired()) {
        previewBusyResponses++;
        sendSnapshotError(503, "camera_io_busy", false);
        return;
    }

    const uint16_t logContextId = allocateLogContextId();
    const unsigned long snapshotStartedAtMs = millis();

    if (!snapshotTarget.valid) {
        if (lastSnapshotResolveAttemptAtMs != 0 && millis() - lastSnapshotResolveAttemptAtMs < SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS) {
            sendSnapshotError(503, "snapshot_temporarily_unavailable");
            return;
        }

        if (!resolveSnapshotTarget(logContextId)) {
            sendSnapshotError(503, "snapshot_unavailable");
            return;
        }
    }

    HTTPClient& request = cameraSnapshotHttpSession.request;
    previewCameraFetches++;
    const int statusCode = openAuthenticatedCameraGet(snapshotTarget.url, logContextId);
    const int contentLength = request.getSize();

    if (statusCode < 200 || statusCode >= 300 || contentLength == 0 || (contentLength > 0 && contentLength < static_cast<int>(MIN_VALID_SNAPSHOT_SIZE_BYTES)) || contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)) {
        finishCameraSnapshotGet(false);
        lastSnapshotResult = statusCode < 0 ? currentTimestamp() + " - " + HTTPClient::errorToString(statusCode) : currentTimestamp() + " - falha HTTP " + String(statusCode);
        sendSnapshotError(502, "invalid_camera_response");
        return;
    }

    WiFiClient* source = request.getStreamPtr();

    if (source == nullptr) {
        finishCameraSnapshotGet(false);
        sendSnapshotError(502, "camera_stream_unavailable");
        return;
    }

    uint8_t prefix[2] = {0, 0};
    size_t prefixLength = 0;
    unsigned long lastDataAtMs = millis();

    while (prefixLength < sizeof(prefix)) {
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

        if (!source->connected() || millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS) {
            break;
        }

        updateCameraAlertLed();
        delay(1);
    }

    if (prefixLength != sizeof(prefix) || prefix[0] != 0xFF || prefix[1] != 0xD8) {
        finishCameraSnapshotGet(false);
        lastSnapshotResult = "Resposta da camera nao e um JPEG valido";
        sendSnapshotError(502, "invalid_jpeg_header");
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

    while (bytesTransferred < MAX_SNAPSHOT_SIZE_BYTES) {
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

        if (!source->connected() || millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS) {
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
    finishCameraSnapshotGet(validTransfer && contentLength > 0 && bytesTransferred == static_cast<size_t>(contentLength));

    if (validTransfer) {
        snapshotsServed++;
        previewBytesTransferred += bytesTransferred;
        lastBrowserSnapshotDeliveredAtMs = millis();
        lastBrowserSnapshotBytes = bytesTransferred;
        lastBrowserSnapshotLatencyMs = lastBrowserSnapshotDeliveredAtMs - snapshotStartedAtMs;
        lastSnapshotResult = currentTimestamp() + " - quadro entregue; " + String(bytesTransferred) + " bytes; perfil " + snapshotTarget.profileName;
    } else if (browserDisconnected) {
        previewClientAborts++;
    } else {
        snapshotErrors++;
        lastSnapshotResult = "Transferencia do snapshot JPEG incompleta";
    }

    static unsigned long lastSuccessfulSnapshotTransferLogAtMs = 0;
    const unsigned long transferFinishedAtMs = millis();
    const bool logTransfer = !validTransfer || lastSuccessfulSnapshotTransferLogAtMs == 0 || transferFinishedAtMs - lastSuccessfulSnapshotTransferLogAtMs >= SNAPSHOT_SUCCESS_LOG_INTERVAL_MS;

    if (logTransfer) {
        PersistentLog::record(PersistentLog::Event::SnapshotTransfer, validTransfer ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, validTransfer ? "browser_jpeg" : browserDisconnected ? "browser_disconnected" : "incomplete_jpeg", browserIp, browserPort, 200, transferFinishedAtMs - snapshotStartedAtMs, static_cast<uint32_t>(bytesTransferred), PersistentLog::FlagNone, PersistentLog::Level::Automatic, false, logContextId);

        if (validTransfer) {
            lastSuccessfulSnapshotTransferLogAtMs = transferFinishedAtMs;
        }
    }
}

ServiceProbe probeRtsp(const IPAddress& ip, uint16_t port, uint16_t contextId)
{
    ServiceProbe result;
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    const String requestUri = String("rtsp://") + ip.toString() + ':' + String(port) + '/';

    WiFiClient client;
    const unsigned long startedAt = millis();
    const TcpConnectionAttempt connection = connectTcp(client, ip, port, TCP_CONNECT_TIMEOUT_MS);

    if (!connection.connected) {
        client.stop();
        result.latencyMs = connection.durationMs;
        PersistentLog::record(PersistentLog::Event::RtspProbe, PersistentLog::Outcome::Failure, connection.detail, ip, port, connection.errorCode, result.latencyMs, TCP_CONNECT_TIMEOUT_MS, PersistentLog::FlagOutbound, PersistentLog::Level::Error, false, logContextId);
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

    while (millis() - responseStartedAt < SERVICE_RESPONSE_TIMEOUT_MS) {
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
    PersistentLog::record(PersistentLog::Event::RtspProbe, result.protocolResponded ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, resultDetail, ip, port, result.statusCode, result.latencyMs, TCP_CONNECT_TIMEOUT_MS, PersistentLog::FlagOutbound, result.protocolResponded ? PersistentLog::Level::Info : PersistentLog::Level::Error, false, logContextId);
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

bool scanSubnetForCamera(uint16_t contextId)
{
    const unsigned long scanStartedAtMs = millis();
    const uint16_t logContextId = contextId != 0 ? contextId : allocateLogContextId();
    subnetScanRuns++;
    lastSubnetScanAtMs = millis();
    lastEvent = currentTimestamp() + " - Varrendo a sub-rede por RTSP e ONVIF";
    Serial.println(lastEvent);
    PersistentLog::record(PersistentLog::Event::SubnetScan, PersistentLog::Outcome::Started, "camera_scan", WiFi.localIP(), CAMERA_RTSP_PORT, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info, false, logContextId);

    const uint32_t local = ipToUint32(WiFi.localIP());
    const uint32_t mask = ipToUint32(WiFi.subnetMask());
    const uint32_t network = local & mask;
    const uint32_t broadcast = network | (~mask);

    if (broadcast <= network + 1) {
        lastScanResult = "Mascara de rede invalida para varredura";
        PersistentLog::record(PersistentLog::Event::SubnetScan, PersistentLog::Outcome::Failure, "invalid_subnet_mask", WiFi.localIP(), CAMERA_RTSP_PORT, 0, millis() - scanStartedAtMs, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Error, false, logContextId);
        return false;
    }

    const uint32_t hostCount = broadcast - network - 1;

    if (hostCount > MAX_SCAN_HOSTS) {
        lastScanResult = "Rede possui " + String(hostCount) + " hosts; limite configurado=" + String(MAX_SCAN_HOSTS);

        Serial.println(lastScanResult);
        PersistentLog::record(PersistentLog::Event::SubnetScan, PersistentLog::Outcome::Skipped, "host_limit_exceeded", WiFi.localIP(), CAMERA_RTSP_PORT, 0, millis() - scanStartedAtMs, hostCount, PersistentLog::FlagScheduled, PersistentLog::Level::Warning, false, logContextId);
        return false;
    }

    const IPAddress ownIp = WiFi.localIP();
    const IPAddress gateway = WiFi.gatewayIP();

    Serial.printf("Varredura de %s ate %s (%lu hosts)\n", uint32ToIp(network + 1).toString().c_str(), uint32ToIp(broadcast - 1).toString().c_str(), static_cast<unsigned long>(hostCount));

    for (uint32_t value = network + 1; value < broadcast; value++) {
        updateCameraAlertLed();
        const IPAddress candidateIp = uint32ToIp(value);

        if (candidateIp == ownIp || candidateIp == gateway) {
            continue;
        }

        httpServer.handleClient();

        if (!tcpPortOpen(candidateIp, CAMERA_RTSP_PORT, SCAN_CONNECT_TIMEOUT_MS, logContextId)) {
            delay(1);
            continue;
        }

        Serial.printf("Porta RTSP aberta em %s\n", candidateIp.toString().c_str());

        for (size_t portIndex = 0; portIndex < ONVIF_PORT_CANDIDATE_COUNT; portIndex++) {
            const uint16_t onvifPort = ONVIF_PORT_CANDIDATES[portIndex];
            CameraTarget candidate;
            candidate.valid = true;
            candidate.ip = candidateIp;
            candidate.onvifPort = onvifPort;
            candidate.rtspPort = CAMERA_RTSP_PORT;
            candidate.onvifPath = DEFAULT_ONVIF_PATH;
            candidate.onvifUrl = makeOnvifUrl(candidate.ip, candidate.onvifPort, candidate.onvifPath);

            const ServiceProbe candidateProbe = probeOnvif(candidate, logContextId);

            if (!candidateProbe.protocolResponded) {
                continue;
            }

            const String oldIp = camera.valid ? camera.ip.toString() : "";
            camera = candidate;
            camera.endpointUuid = "";
            camera.scopes = "Localizada por varredura RTSP/ONVIF";
            lastOnvifProbe = candidateProbe;
            invalidateSnapshotTarget();
            invalidateOnvifServiceCatalog();
            resetSnapshotProfileFailover();
            lastSnapshotResolveAttemptAtMs = 0;
            consecutiveFailures = 0;
            consecutiveSuccesses = 0;
            consecutiveControlFailures = 0;
            consecutiveInconclusiveChecks = 0;
            consecutiveMediaServiceFailures = 0;
            snapshotTargetFailureStreak = 0;
            saveCameraTarget(logContextId);

            lastScanResult = currentTimestamp() + " - camera candidata em " + camera.onvifUrl;

            Serial.println(lastScanResult);
            PersistentLog::record(PersistentLog::Event::SubnetScan, PersistentLog::Outcome::Success, "camera_found", camera.ip, camera.onvifPort, 1, millis() - scanStartedAtMs, hostCount, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true, logContextId);

            if (!oldIp.isEmpty() && oldIp != camera.ip.toString()) {
                sendIpChangeAlert(oldIp, camera.ip.toString());
            }

            return true;
        }
    }

    lastScanResult = currentTimestamp() + " - nenhuma camera localizada";
    Serial.println(lastScanResult);
    PersistentLog::record(PersistentLog::Event::SubnetScan, PersistentLog::Outcome::Failure, "camera_not_found", IPAddress(), CAMERA_RTSP_PORT, 0, millis() - scanStartedAtMs, hostCount, PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true, logContextId);
    return false;
}

}
