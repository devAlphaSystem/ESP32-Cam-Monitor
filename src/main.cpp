#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <esp_bt.h>
#include <esp_system.h>
#include <time.h>

#include "app_config.h"
#if RELEASE_BLUETOOTH_MEMORY
#include <esp32-hal-bt.h>
#endif
#include "dashboard_page.h"
#include "secrets.h"

namespace {

constexpr char HOSTNAME[] = "camera-watchdog";
constexpr char FIRMWARE_VERSION[] = "0.3.4";
constexpr char RESEND_API_URL[] = "https://api.resend.com/emails";
constexpr char NVS_NAMESPACE[] = "camwatch";

const IPAddress WS_DISCOVERY_MULTICAST(239, 255, 255, 250);

constexpr char SOAP_GET_SYSTEM_TIME[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">"
    "<s:Body><tds:GetSystemDateAndTime/></s:Body>"
    "</s:Envelope>";

enum class CameraState {
    Starting,
    Discovering,
    Online,
    Degraded,
    Offline
};

struct ParsedUrl {
    bool valid = false;
    bool secure = false;
    IPAddress ip;
    uint16_t port = 0;
    String path;
};

struct ParsedHttpResource {
    bool valid = false;
    bool secure = false;
    String host;
    uint16_t port = 0;
    String path;
};

struct CameraTarget {
    bool valid = false;
    IPAddress ip;
    uint16_t onvifPort = 0;
    uint16_t rtspPort = CAMERA_RTSP_PORT;
    String onvifPath;
    String onvifUrl;
    String endpointUuid;
    String scopes;
};

struct ServiceProbe {
    bool tcpConnected = false;
    bool protocolResponded = false;
    int statusCode = 0;
    uint32_t latencyMs = 0;
};

struct DiscoveryCandidate {
    CameraTarget target;
    String rawSummary;
};

struct SnapshotTarget {
    bool valid = false;
    String url;
    String profileToken;
    String profileName;
};

WebServer httpServer(HTTP_SERVER_PORT);
WiFiUDP discoveryUdp;
Preferences preferences;

CameraTarget camera;
SnapshotTarget snapshotTarget;
ServiceProbe lastOnvifProbe;
ServiceProbe lastRtspProbe;
CameraState cameraState = CameraState::Starting;

char discoveryBuffer[WS_DISCOVERY_BUFFER_SIZE + 1];
DiscoveryCandidate discovered[MAX_DISCOVERED_CAMERAS];
size_t discoveredCount = 0;

unsigned long bootAtMs = 0;
unsigned long lastHealthCheckAtMs = 0;
unsigned long lastRediscoveryAtMs = 0;
unsigned long lastSubnetScanAtMs = 0;
unsigned long lastEmailAttemptAtMs = 0;
unsigned long offlineStartedAtMs = 0;
unsigned long lastStateChangeAtMs = 0;
unsigned long lastCameraAlertLedToggleAtMs = 0;

uint32_t totalHealthChecks = 0;
uint32_t successfulHealthChecks = 0;
uint32_t failedHealthChecks = 0;
uint32_t discoveryRuns = 0;
uint32_t subnetScanRuns = 0;
uint32_t emailsSent = 0;
uint32_t emailErrors = 0;
uint32_t snapshotsServed = 0;
uint32_t snapshotErrors = 0;
uint32_t heapBeforeBluetoothReleaseBytes = 0;
uint32_t heapAfterBluetoothReleaseBytes = 0;
uint32_t bluetoothMemoryReleasedBytes = 0;
uint8_t consecutiveFailures = 0;
uint8_t consecutiveSuccesses = 0;

bool offlineAlertSent = false;
bool pendingOfflineEmail = false;
bool pendingRecoveryEmail = false;
bool cameraAlertLedOn = false;
bool cameraAlertLedBlinking = false;
bool bluetoothMemoryReleaseAttempted = false;
bool bluetoothStartedBeforeRelease = false;
esp_err_t bluetoothMemoryReleaseResult = ESP_ERR_NOT_SUPPORTED;
esp_bt_controller_status_t bluetoothControllerStatusBeforeRelease =
    ESP_BT_CONTROLLER_STATUS_NUM;

String lastEvent = "Inicializando";
String lastEmailResult = "Nenhum e-mail enviado";
String lastDiscoveryResult = "Ainda nao executada";
String lastScanResult = "Ainda nao executada";
String previousCameraIp;
String lastSnapshotResult = "Ainda nao solicitada";
unsigned long lastSnapshotResolveAttemptAtMs = 0;

const char* stateName(CameraState state)
{
    switch (state) {
        case CameraState::Starting:
            return "starting";
        case CameraState::Discovering:
            return "discovering";
        case CameraState::Online:
            return "online";
        case CameraState::Degraded:
            return "degraded";
        case CameraState::Offline:
            return "offline";
    }

    return "unknown";
}

void writeCameraAlertLed(bool on)
{
    cameraAlertLedOn = on;
    digitalWrite(CAMERA_ALERT_LED_PIN, on ? HIGH : LOW);
}

void configureCameraAlertLed()
{
    pinMode(CAMERA_ALERT_LED_PIN, OUTPUT);
    writeCameraAlertLed(false);
    lastCameraAlertLedToggleAtMs = millis();
}

void updateCameraAlertLed()
{
    const bool shouldBlink = cameraState != CameraState::Online;
    const unsigned long now = millis();

    if (!shouldBlink) {
        cameraAlertLedBlinking = false;
        lastCameraAlertLedToggleAtMs = now;

        if (cameraAlertLedOn) {
            writeCameraAlertLed(false);
        }

        return;
    }

    if (!cameraAlertLedBlinking) {
        cameraAlertLedBlinking = true;
        lastCameraAlertLedToggleAtMs = now;
        writeCameraAlertLed(true);
        return;
    }

    if (
        now - lastCameraAlertLedToggleAtMs >=
        CAMERA_ALERT_LED_BLINK_INTERVAL_MS
    ) {
        lastCameraAlertLedToggleAtMs = now;
        writeCameraAlertLed(!cameraAlertLedOn);
    }
}

void releaseUnusedBluetoothMemory()
{
    heapBeforeBluetoothReleaseBytes = ESP.getFreeHeap();
    heapAfterBluetoothReleaseBytes = heapBeforeBluetoothReleaseBytes;

#if RELEASE_BLUETOOTH_MEMORY
    bluetoothMemoryReleaseAttempted = true;
    bluetoothControllerStatusBeforeRelease =
        esp_bt_controller_get_status();

    // O core Arduino libera apenas a memoria do controlador antes de setup()
    // quando esp32-hal-bt nao e vinculado. Esta referencia adia essa liberacao
    // para que controlador e pilha host sejam devolvidos juntos logo abaixo.
    bluetoothStartedBeforeRelease = btStarted();
    bluetoothMemoryReleaseResult = esp_bt_mem_release(ESP_BT_MODE_BTDM);
    heapAfterBluetoothReleaseBytes = ESP.getFreeHeap();

    if (heapAfterBluetoothReleaseBytes > heapBeforeBluetoothReleaseBytes) {
        bluetoothMemoryReleasedBytes =
            heapAfterBluetoothReleaseBytes -
            heapBeforeBluetoothReleaseBytes;
    }

    if (bluetoothMemoryReleaseResult == ESP_OK) {
        Serial.printf(
            "Memoria Bluetooth liberada: %u bytes.\n",
            bluetoothMemoryReleasedBytes
        );
    } else if (bluetoothMemoryReleaseResult == ESP_ERR_NOT_FOUND) {
        Serial.println(
            "Memoria Bluetooth ja estava liberada ou nao foi reservada."
        );
    } else {
        Serial.printf(
            "Nao foi possivel liberar memoria Bluetooth: %d\n",
            static_cast<int>(bluetoothMemoryReleaseResult)
        );
    }

    Serial.printf(
        "Heap Bluetooth: antes=%u depois=%u diferenca=%u bytes\n",
        heapBeforeBluetoothReleaseBytes,
        heapAfterBluetoothReleaseBytes,
        bluetoothMemoryReleasedBytes
    );
#else
    Serial.println("Liberacao da memoria Bluetooth desativada.");
#endif
}

void appendJsonEscaped(String& output, const String& input)
{
    for (size_t index = 0; index < input.length(); index++) {
        const char value = input.charAt(index);

        switch (value) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (static_cast<uint8_t>(value) >= 0x20) {
                    output += value;
                }
                break;
        }
    }
}

String jsonEscape(const String& input)
{
    String output;
    output.reserve(input.length() + 16);
    appendJsonEscaped(output, input);
    return output;
}

String htmlEscape(const String& input)
{
    String output;
    output.reserve(input.length() + 16);

    for (size_t index = 0; index < input.length(); index++) {
        const char value = input.charAt(index);

        switch (value) {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '"':
                output += "&quot;";
                break;
            case '\'':
                output += "&#39;";
                break;
            default:
                output += value;
                break;
        }
    }

    return output;
}

String currentTimestamp()
{
    const time_t now = time(nullptr);

    if (now < 1700000000) {
        return String("uptime-") + String(millis() / 1000UL) + "s";
    }

    struct tm utcTime;
    gmtime_r(&now, &utcTime);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return String(buffer);
}

String durationText(unsigned long durationMs)
{
    const unsigned long totalSeconds = durationMs / 1000UL;
    const unsigned long hours = totalSeconds / 3600UL;
    const unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
    const unsigned long seconds = totalSeconds % 60UL;

    char buffer[48];
    snprintf(
        buffer,
        sizeof(buffer),
        "%luh %lum %lus",
        hours,
        minutes,
        seconds
    );

    return String(buffer);
}

uint32_t ipToUint32(const IPAddress& ip)
{
    return
        (static_cast<uint32_t>(ip[0]) << 24) |
        (static_cast<uint32_t>(ip[1]) << 16) |
        (static_cast<uint32_t>(ip[2]) << 8) |
        static_cast<uint32_t>(ip[3]);
}

IPAddress uint32ToIp(uint32_t value)
{
    return IPAddress(
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF)
    );
}

bool parseHttpResourceUrl(
    const String& url,
    ParsedHttpResource& parsed
)
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
    const int hostEnd =
        pathStart >= 0 ? pathStart : normalizedUrl.length();
    String authority = normalizedUrl.substring(hostStart, hostEnd);
    parsed.path =
        pathStart >= 0 ? normalizedUrl.substring(pathStart) : "/";

    if (
        authority.isEmpty() ||
        authority.indexOf('@') >= 0 ||
        parsed.path.indexOf('\r') >= 0 ||
        parsed.path.indexOf('\n') >= 0
    ) {
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

String makeOnvifUrl(
    const IPAddress& ip,
    uint16_t port,
    const String& path
)
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

bool extractLocalTag(
    const String& xml,
    const char* localName,
    int fromPosition,
    String& value,
    int& nextPosition
)
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

        const char beforeLocalName =
            namePosition > openStart
                ? xml.charAt(namePosition - 1)
                : '\0';

        if (beforeLocalName != '<' && beforeLocalName != ':') {
            searchPosition = namePosition + marker.length();
            continue;
        }

        if (
            openStart + 1 < static_cast<int>(xml.length()) &&
            xml.charAt(openStart + 1) == '/'
        ) {
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

    while (
        start < static_cast<int>(text.length()) &&
        isspace(static_cast<unsigned char>(text.charAt(start)))
    ) {
        start++;
    }

    int end = start;

    while (
        end < static_cast<int>(text.length()) &&
        !isspace(static_cast<unsigned char>(text.charAt(end)))
    ) {
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

bool extractXmlAttribute(
    const String& openingTag,
    const char* attributeName,
    String& value
)
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

bool findNextOpeningTag(
    const String& xml,
    const char* localName,
    int fromPosition,
    int& openingStart,
    int& openingEnd
)
{
    int searchPosition = fromPosition;
    const size_t localNameLength = strlen(localName);

    while (true) {
        const int namePosition = xml.indexOf(localName, searchPosition);

        if (namePosition < 0) {
            return false;
        }

        const int nameEnd = namePosition + localNameLength;
        const char before =
            namePosition > 0 ? xml.charAt(namePosition - 1) : '\0';
        const char after =
            nameEnd < static_cast<int>(xml.length())
                ? xml.charAt(nameEnd)
                : '\0';

        if (
            (before == '<' || before == ':') &&
            (
                after == '>' ||
                after == '/' ||
                isspace(static_cast<unsigned char>(after))
            )
        ) {
            openingStart = xml.lastIndexOf('<', namePosition);

            if (
                openingStart >= 0 &&
                openingStart + 1 < static_cast<int>(xml.length()) &&
                xml.charAt(openingStart + 1) != '/'
            ) {
                openingEnd = xml.indexOf('>', nameEnd);
                return openingEnd >= 0;
            }
        }

        searchPosition = nameEnd;
    }
}

bool selectSnapshotProfile(
    const String& profilesXml,
    String& selectedToken,
    String& selectedName
)
{
    int searchPosition = 0;
    String fallbackToken;
    String fallbackName;

    while (true) {
        int profileStart = 0;
        int profileTagEnd = 0;

        if (!findNextOpeningTag(
            profilesXml,
            "Profiles",
            searchPosition,
            profileStart,
            profileTagEnd
        )) {
            break;
        }

        const String openingTag =
            profilesXml.substring(profileStart, profileTagEnd + 1);
        String profileToken;

        if (!extractXmlAttribute(openingTag, "token", profileToken)) {
            searchPosition = profileTagEnd + 1;
            continue;
        }

        int nextProfileStart = profilesXml.length();
        int ignoredTagEnd = 0;
        findNextOpeningTag(
            profilesXml,
            "Profiles",
            profileTagEnd + 1,
            nextProfileStart,
            ignoredTagEnd
        );

        String profileName;
        int nameEnd = 0;

        if (
            !extractLocalTag(
                profilesXml,
                "Name",
                profileTagEnd + 1,
                profileName,
                nameEnd
            ) ||
            nameEnd > nextProfileStart
        ) {
            profileName = profileToken;
        }

        if (fallbackToken.isEmpty()) {
            fallbackToken = profileToken;
            fallbackName = profileName;
        }

        if (
            !String(CAMERA_SNAPSHOT_PROFILE_MATCH).isEmpty() &&
            containsIgnoreCase(profileName, CAMERA_SNAPSHOT_PROFILE_MATCH)
        ) {
            selectedToken = profileToken;
            selectedName = profileName;
            return true;
        }

        searchPosition = profileTagEnd + 1;
    }

    if (fallbackToken.isEmpty()) {
        return false;
    }

    selectedToken = fallbackToken;
    selectedName = fallbackName;
    return true;
}

void invalidateSnapshotTarget()
{
    snapshotTarget = SnapshotTarget{};
}

void setState(CameraState newState, const String& event)
{
    if (cameraState != newState) {
        cameraState = newState;
        lastStateChangeAtMs = millis();
    }

    lastEvent = currentTimestamp() + " - " + event;
    Serial.printf("ESTADO: %s | %s\n", stateName(cameraState), event.c_str());
}

bool resendConfigured()
{
    return
        String(RESEND_API_KEY).startsWith("re_") &&
        String(RESEND_API_KEY).indexOf("COLE_SUA") < 0 &&
        String(RESEND_FROM).indexOf("seudominio") < 0 &&
        String(ALERT_EMAIL).indexOf("exemplo.com") < 0;
}

bool sendResendEmail(const String& subject, const String& html)
{
    lastEmailAttemptAtMs = millis();

    if (!resendConfigured()) {
        lastEmailResult = "Resend nao configurado em include/secrets.h";
        Serial.println(lastEmailResult);
        emailErrors++;
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        lastEmailResult = "Wi-Fi desconectado; envio adiado";
        emailErrors++;
        return false;
    }

    WiFiClientSecure secureClient;

    // MVP pessoal: a conexao continua criptografada, mas o certificado do
    // servidor nao e validado. Para produto, use uma CA confiavel.
    secureClient.setInsecure();

    HTTPClient request;
    request.setConnectTimeout(10000);
    request.setTimeout(15000);
    request.setReuse(false);

    if (!request.begin(secureClient, RESEND_API_URL)) {
        lastEmailResult = "Falha ao inicializar cliente HTTPS";
        emailErrors++;
        return false;
    }

    request.addHeader("Authorization", String("Bearer ") + RESEND_API_KEY);
    request.addHeader("Content-Type", "application/json");
    request.addHeader(
        "User-Agent",
        String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION
    );

    // O corpo e escapado direto no payload: uma String intermediaria por campo
    // dobraria o pico de heap justamente enquanto o mbedTLS mantem seus buffers.
    // O HTML e cheio de aspas em estilos inline e cada uma vira \" ao ser
    // escapada; a folga proporcional evita realocar no meio da montagem.
    String payload;
    payload.reserve(subject.length() + html.length() + html.length() / 4 + 512);
    payload += "{\"from\":\"";
    appendJsonEscaped(payload, RESEND_FROM);
    payload += "\",\"to\":[\"";
    appendJsonEscaped(payload, ALERT_EMAIL);
    payload += "\"],\"subject\":\"";
    appendJsonEscaped(payload, subject);
    payload += "\",\"html\":\"";
    appendJsonEscaped(payload, html);
    payload += "\"}";

    const int statusCode = request.POST(payload);
    const String responseBody = request.getString();
    request.end();

    if (statusCode >= 200 && statusCode < 300) {
        emailsSent++;
        lastEmailResult =
            currentTimestamp() +
            " - enviado; HTTP " +
            String(statusCode) +
            "; " +
            responseBody;

        Serial.println(lastEmailResult);
        return true;
    }

    emailErrors++;
    lastEmailResult =
        currentTimestamp() +
        " - erro; HTTP " +
        String(statusCode) +
        "; " +
        responseBody;

    Serial.println(lastEmailResult);
    return false;
}

// ---------------------------------------------------------------------------
// Modelo de e-mail
//
// Clientes de e-mail ignoram folhas de estilo, flexbox e grid, entao o layout
// usa tabelas aninhadas e estilo inline: o mesmo resultado no Gmail, no Proton
// e no Outlook. As legendas aqui sao acentuadas porque nunca vao ao serial.
// ---------------------------------------------------------------------------

struct EmailTheme {
    const char* accent;
    const char* soft;
};

constexpr EmailTheme EMAIL_THEME_OK{"#0d9668", "#e6f7f0"};
constexpr EmailTheme EMAIL_THEME_ALERT{"#dc2626", "#fdecec"};
constexpr EmailTheme EMAIL_THEME_INFO{"#2563eb", "#eaf1fe"};

const char* stateLabel(CameraState state)
{
    switch (state) {
        case CameraState::Starting:
            return "iniciando";
        case CameraState::Discovering:
            return "procurando a câmera";
        case CameraState::Online:
            return "online";
        case CameraState::Degraded:
            return "instável";
        case CameraState::Offline:
            return "offline";
    }

    return "desconhecido";
}

String describeProbe(const ServiceProbe& probe, const char* protocol)
{
    if (!probe.tcpConnected) {
        return "sem conexão TCP";
    }

    String text;

    if (probe.protocolResponded) {
        text = "respondeu ";
        text += protocol;
        text += ' ';
        text += probe.statusCode;
    } else {
        text = "porta aberta, sem resposta do protocolo";
    }

    text += " · ";
    text += probe.latencyMs;
    text += " ms";
    return text;
}

void appendDetailRow(
    String& html,
    const char* label,
    const String& value,
    uint8_t index
)
{
    html += "<tr style=\"background:";
    html += (index % 2) ? "#f8fafc" : "#ffffff";
    html += "\"><td style=\"padding:11px 18px;font-size:13px;line-height:1.45;"
            "color:#64748b;white-space:nowrap;vertical-align:top;";

    if (index > 0) {
        html += "border-top:1px solid #edf2f7;";
    }

    html += "\">";
    html += htmlEscape(label);
    html += "</td><td style=\"padding:11px 18px;font-size:13px;line-height:1.45;"
            "color:#0f1b2b;font-weight:600;word-break:break-word;vertical-align:top;";

    if (index > 0) {
        html += "border-top:1px solid #edf2f7;";
    }

    html += "\">";
    html += htmlEscape(value);
    html += "</td></tr>";
}

void appendDetailsTable(String& html)
{
    html += "<tr><td style=\"padding:24px 28px 0\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" style=\"border-collapse:separate;"
            "border-spacing:0;border:1px solid #e3e9f0;border-radius:10px;"
            "overflow:hidden\">";

    uint8_t index = 0;

    const auto row = [&](const char* label, const String& value) {
        appendDetailRow(html, label, value, index);
        index++;
    };

    row("Câmera", CAMERA_NAME);
    row("Estado", stateLabel(cameraState));
    row("Endereço IP", camera.valid ? camera.ip.toString() : "não localizado");
    row(
        "Endpoint ONVIF",
        camera.onvifUrl.isEmpty() ? String("desconhecido") : camera.onvifUrl
    );
    row("Teste ONVIF", describeProbe(lastOnvifProbe, "HTTP"));
    row("Teste RTSP", describeProbe(lastRtspProbe, "RTSP"));
    row("Falhas consecutivas", String(consecutiveFailures));
    row(
        "Verificações",
        String(totalHealthChecks) + " no total · " +
            String(failedHealthChecks) + " com falha"
    );
    row(
        "ESP32",
        WiFi.localIP().toString() + " · " + WiFi.SSID() + " · " +
            String(WiFi.RSSI()) + " dBm"
    );
    row("Ligado há", durationText(millis()));
    row("Horário (UTC)", currentTimestamp());

    html += "</table></td></tr>";
}

void appendCallout(String& html, const EmailTheme& theme, const String& text)
{
    html += "<tr><td style=\"padding:22px 28px 0\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\"><tr><td style=\"padding:13px 17px;"
            "background:";
    html += theme.soft;
    html += ";border-left:3px solid ";
    html += theme.accent;
    html += ";border-radius:0 8px 8px 0;font-size:14px;line-height:1.5;color:#334155\">";
    html += text;
    html += "</td></tr></table></td></tr>";
}

String buildEmailHtml(
    const EmailTheme& theme,
    const char* badge,
    const char* title,
    const char* intro,
    const String& callout
)
{
    // String::changeBuffer cresce de 16 em 16 bytes e realoca a cada passo, entao
    // a reserva precisa cobrir o documento inteiro de uma vez. O corpo montado
    // fica em torno de 7,3 KB com as 11 linhas de detalhe.
    String html;
    html.reserve(8192);

    html += "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<meta name=\"x-apple-disable-message-reformatting\"></head>"
            "<body style=\"margin:0;padding:0;background:#eef2f7\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" style=\"background:#eef2f7;"
            "padding:26px 12px\"><tr><td align=\"center\">"
            "<table role=\"presentation\" width=\"600\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" style=\"width:100%;max-width:600px;"
            "background:#ffffff;border:1px solid #dfe6ee;border-radius:14px;"
            "overflow:hidden;font-family:-apple-system,BlinkMacSystemFont,"
            "'Segoe UI',Roboto,Helvetica,Arial,sans-serif\">";

    // Faixa superior colorida conforme a gravidade do aviso.
    html += "<tr><td style=\"height:4px;line-height:4px;font-size:0;background:";
    html += theme.accent;
    html += "\">&nbsp;</td></tr>";

    html += "<tr><td style=\"padding:26px 28px 0\">"
            "<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\"><tr>"
            "<td style=\"font-size:11px;font-weight:700;letter-spacing:.11em;"
            "text-transform:uppercase;color:#94a3b8\">Camera Watchdog</td>"
            "<td align=\"right\"><span style=\"display:inline-block;"
            "padding:5px 12px;border-radius:999px;font-size:11px;font-weight:700;"
            "letter-spacing:.05em;background:";
    html += theme.soft;
    html += ";color:";
    html += theme.accent;
    html += "\">";
    html += badge;
    html += "</span></td></tr></table></td></tr>";

    html += "<tr><td style=\"padding:16px 28px 0\">"
            "<h1 style=\"margin:0;font-size:23px;line-height:1.25;color:#0f1b2b;"
            "font-weight:700;letter-spacing:-.01em\">";
    html += title;
    html += "</h1><p style=\"margin:9px 0 0;font-size:15px;line-height:1.6;"
            "color:#51617a\">";
    html += intro;
    html += "</p></td></tr>";

    if (!callout.isEmpty()) {
        appendCallout(html, theme, callout);
    }

    appendDetailsTable(html);

    html += "<tr><td style=\"padding:24px 28px 0\">"
            "<a href=\"http://";
    html += WiFi.localIP().toString();
    html += "\" style=\"display:inline-block;background:#0f1b2b;color:#ffffff;"
            "text-decoration:none;padding:12px 22px;border-radius:9px;"
            "font-size:14px;font-weight:600\">Abrir o painel do dispositivo</a>"
            "</td></tr>";

    html += "<tr><td style=\"padding:24px 28px 26px\">"
            "<div style=\"border-top:1px solid #edf2f7;padding-top:16px;"
            "font-size:11.5px;line-height:1.6;color:#94a3b8\">"
            "Mensagem automática do ESP32 Camera Watchdog · firmware ";
    html += FIRMWARE_VERSION;
    html += " · painel em http://";
    html += WiFi.localIP().toString();
    html += "</div></td></tr>";

    html += "</table></td></tr></table></body></html>";
    return html;
}

bool sendOfflineAlert()
{
    String subject = "[Camera offline] ";
    subject += CAMERA_NAME;

    String callout = "Falhas consecutivas: <strong>";
    callout += consecutiveFailures;
    callout += "</strong>. Nem o serviço ONVIF nem a porta RTSP responderam "
               "aos últimos testes.";

    const String html = buildEmailHtml(
        EMAIL_THEME_ALERT,
        "OFFLINE",
        "A câmera parou de responder",
        "O ESP32 confirmou a falha antes de avisar: o alerta só é enviado "
        "depois de várias tentativas seguidas sem resposta.",
        callout
    );

    const bool sent = sendResendEmail(subject, html);

    if (sent) {
        offlineAlertSent = true;
        pendingOfflineEmail = false;
    }

    return sent;
}

bool sendRecoveryAlert()
{
    String subject = "[Camera recuperada] ";
    subject += CAMERA_NAME;

    String callout;

    if (offlineStartedAtMs > 0) {
        callout = "Tempo aproximado fora do ar: <strong>";
        callout += durationText(millis() - offlineStartedAtMs);
        callout += "</strong>.";
    }

    const String html = buildEmailHtml(
        EMAIL_THEME_OK,
        "RECUPERADA",
        "A câmera voltou a responder",
        "O monitoramento normal foi retomado. Nenhuma ação é necessária.",
        callout
    );

    const bool sent = sendResendEmail(subject, html);

    if (sent) {
        pendingRecoveryEmail = false;
    }

    return sent;
}

bool sendIpChangeAlert(const String& oldIp, const String& newIp)
{
    if (!EMAIL_ON_IP_CHANGE || oldIp.isEmpty() || oldIp == newIp) {
        return true;
    }

    String subject = "[Camera mudou de IP] ";
    subject += CAMERA_NAME;

    String callout = "<strong>";
    callout += htmlEscape(oldIp);
    callout += "</strong> &rarr; <strong>";
    callout += htmlEscape(newIp);
    callout += "</strong>";

    const String html = buildEmailHtml(
        EMAIL_THEME_INFO,
        "NOVO IP",
        "A câmera mudou de endereço",
        "O ESP32 localizou a câmera em um novo IP e já atualizou o endpoint "
        "monitorado. Atualize favoritos e gravadores que apontem para o "
        "endereço antigo.",
        callout
    );

    return sendResendEmail(subject, html);
}

void saveCameraTarget()
{
    if (!camera.valid) {
        return;
    }

    preferences.begin(NVS_NAMESPACE, false);
    preferences.putString("cam_url", camera.onvifUrl);
    preferences.putString("cam_uuid", camera.endpointUuid);
    preferences.putString("cam_scope", camera.scopes);
    preferences.putUShort("rtsp_port", camera.rtspPort);
    preferences.end();
}

bool loadCameraTarget()
{
    preferences.begin(NVS_NAMESPACE, true);
    const String savedUrl = preferences.getString("cam_url", "");
    const String savedUuid = preferences.getString("cam_uuid", "");
    const String savedScopes = preferences.getString("cam_scope", "");
    const uint16_t savedRtspPort =
        preferences.getUShort("rtsp_port", CAMERA_RTSP_PORT);
    preferences.end();

    if (savedUrl.isEmpty()) {
        return false;
    }

    ParsedUrl parsed;

    if (!parseHttpUrl(savedUrl, parsed) || parsed.secure) {
        return false;
    }

    camera.valid = true;
    camera.ip = parsed.ip;
    camera.onvifPort = parsed.port;
    camera.rtspPort = savedRtspPort;
    camera.onvifPath = parsed.path;
    camera.onvifUrl = savedUrl;
    camera.endpointUuid = savedUuid;
    camera.scopes = savedScopes;

    Serial.printf("Camera carregada da NVS: %s\n", camera.onvifUrl.c_str());
    return true;
}

bool loadCameraIpHint()
{
    if (String(CAMERA_IP_HINT).isEmpty()) {
        return false;
    }

    IPAddress hintedIp;

    if (!hintedIp.fromString(CAMERA_IP_HINT)) {
        Serial.println("CAMERA_IP_HINT invalido.");
        return false;
    }

    camera.valid = true;
    camera.ip = hintedIp;
    camera.onvifPort = ONVIF_PORT_CANDIDATES[0];
    camera.rtspPort = CAMERA_RTSP_PORT;
    camera.onvifPath = DEFAULT_ONVIF_PATH;
    camera.onvifUrl = makeOnvifUrl(
        camera.ip,
        camera.onvifPort,
        camera.onvifPath
    );

    Serial.printf("Usando CAMERA_IP_HINT: %s\n", camera.ip.toString().c_str());
    return true;
}

String makeMessageUuid()
{
    char buffer[64];
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    const uint32_t c = esp_random();
    const uint32_t d = esp_random();

    snprintf(
        buffer,
        sizeof(buffer),
        "uuid:%08lx-%04lx-%04lx-%04lx-%04lx%08lx",
        static_cast<unsigned long>(a),
        static_cast<unsigned long>((b >> 16) & 0xFFFF),
        static_cast<unsigned long>(b & 0xFFFF),
        static_cast<unsigned long>((c >> 16) & 0xFFFF),
        static_cast<unsigned long>(c & 0xFFFF),
        static_cast<unsigned long>(d)
    );

    return String(buffer);
}

bool sendDiscoveryProbe(const char* typeName)
{
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
        return false;
    }

    const size_t written = discoveryUdp.write(
        reinterpret_cast<const uint8_t*>(xml.c_str()),
        xml.length()
    );

    return written == xml.length() && discoveryUdp.endPacket() == 1;
}

bool candidateAlreadyAdded(const CameraTarget& target)
{
    for (size_t index = 0; index < discoveredCount; index++) {
        if (
            discovered[index].target.ip == target.ip &&
            discovered[index].target.onvifPort == target.onvifPort &&
            discovered[index].target.onvifPath == target.onvifPath
        ) {
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

        if (!extractLocalTag(
            xml,
            "XAddrs",
            searchPosition,
            xaddrs,
            nextXaddrs
        )) {
            break;
        }

        String endpointUuid;
        String scopes;
        int ignored = 0;
        extractLocalTag(xml, "Address", searchPosition, endpointUuid, ignored);
        extractLocalTag(xml, "Scopes", searchPosition, scopes, ignored);

        const String firstUrl = firstWhitespaceToken(xaddrs);
        ParsedUrl parsed;

        if (
            parseHttpUrl(firstUrl, parsed) &&
            !parsed.secure
        ) {
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
                discovered[discoveredCount].rawSummary =
                    endpointUuid + " " + scopes + " " + firstUrl;

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

    if (
        !camera.endpointUuid.isEmpty() &&
        containsIgnoreCase(combined, camera.endpointUuid)
    ) {
        score += 1000;
    }

    if (
        camera.valid &&
        candidate.target.ip == camera.ip
    ) {
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

bool applyBestDiscoveredCandidate()
{
    if (discoveredCount == 0) {
        return false;
    }

    int bestScore = -1;
    int bestIndex = -1;

    for (size_t index = 0; index < discoveredCount; index++) {
        const int score = scoreCandidate(discovered[index]);

        Serial.printf(
            "ONVIF candidato %u: %s | score=%d\n",
            static_cast<unsigned>(index + 1),
            discovered[index].target.onvifUrl.c_str(),
            score
        );

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
    lastSnapshotResolveAttemptAtMs = 0;
    saveCameraTarget();

    const String newIp = camera.ip.toString();

    Serial.printf("Camera selecionada: %s\n", camera.onvifUrl.c_str());

    if (!oldIp.isEmpty() && oldIp != newIp) {
        previousCameraIp = oldIp;
        sendIpChangeAlert(oldIp, newIp);
    }

    return true;
}

bool discoverOnvifCamera()
{
    discoveryRuns++;
    lastRediscoveryAtMs = millis();
    lastEvent = currentTimestamp() + " - Executando ONVIF WS-Discovery";
    Serial.println(lastEvent);
    discoveredCount = 0;

    discoveryUdp.stop();

    if (!discoveryUdp.begin(WS_DISCOVERY_LOCAL_PORT)) {
        lastDiscoveryResult = "Falha ao abrir socket UDP de descoberta";
        return false;
    }

    while (discoveryUdp.parsePacket() > 0) {
        while (discoveryUdp.available()) {
            discoveryUdp.read();
        }
    }

    const bool sentDevice = sendDiscoveryProbe("tds:Device");
    delay(40);
    const bool sentNvt = sendDiscoveryProbe("dn:NetworkVideoTransmitter");
    delay(40);
    const bool sentAny = sendDiscoveryProbe(nullptr);

    if (!sentDevice && !sentNvt && !sentAny) {
        lastDiscoveryResult = "Nao foi possivel enviar o Probe multicast";
        discoveryUdp.stop();
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

        const size_t bytesToRead = min(
            static_cast<size_t>(packetSize),
            WS_DISCOVERY_BUFFER_SIZE
        );

        const int bytesRead = discoveryUdp.read(
            reinterpret_cast<uint8_t*>(discoveryBuffer),
            bytesToRead
        );

        if (bytesRead <= 0) {
            continue;
        }

        discoveryBuffer[bytesRead] = '\0';
        addDiscoveryResponse(String(discoveryBuffer));
    }

    discoveryUdp.stop();

    const bool selected = applyBestDiscoveredCandidate();
    lastDiscoveryResult =
        currentTimestamp() +
        " - candidatos=" +
        String(discoveredCount) +
        "; selecionada=" +
        (selected ? camera.onvifUrl : String("nenhuma"));

    Serial.println(lastDiscoveryResult);
    return selected;
}

ServiceProbe probeOnvif(const CameraTarget& target)
{
    ServiceProbe result;

    if (!target.valid || target.onvifUrl.isEmpty()) {
        return result;
    }

    ParsedUrl parsed;

    if (!parseHttpUrl(target.onvifUrl, parsed) || parsed.secure) {
        return result;
    }

    WiFiClient client;
    const unsigned long startedAt = millis();

    if (!client.connect(parsed.ip, parsed.port, TCP_CONNECT_TIMEOUT_MS)) {
        result.latencyMs = millis() - startedAt;
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

    const int firstLineEnd = responseHeader.indexOf("\r\n");
    const String firstLine = firstLineEnd >= 0
        ? responseHeader.substring(0, firstLineEnd)
        : responseHeader;

    if (firstLine.startsWith("HTTP/")) {
        const int firstSpace = firstLine.indexOf(' ');

        if (firstSpace >= 0) {
            result.statusCode = firstLine.substring(firstSpace + 1).toInt();
        }
    }

    String normalizedHeader = responseHeader;
    normalizedHeader.toLowerCase();

    result.protocolResponded =
        result.statusCode == 401 ||
        result.statusCode == 403 ||
        normalizedHeader.indexOf("content-type: application/soap+xml") >= 0 ||
        normalizedHeader.indexOf("content-type: text/xml") >= 0;

    return result;
}

bool resolveCameraOnvifEndpoint()
{
    if (!camera.valid) {
        return false;
    }

    ServiceProbe currentProbe = probeOnvif(camera);

    if (currentProbe.protocolResponded) {
        lastOnvifProbe = currentProbe;
        return true;
    }

    for (size_t portIndex = 0;
         portIndex < ONVIF_PORT_CANDIDATE_COUNT;
         portIndex++) {
        const uint16_t candidatePort = ONVIF_PORT_CANDIDATES[portIndex];

        if (
            candidatePort == camera.onvifPort &&
            camera.onvifPath == DEFAULT_ONVIF_PATH
        ) {
            continue;
        }

        CameraTarget candidate = camera;
        candidate.onvifPort = candidatePort;
        candidate.onvifPath = DEFAULT_ONVIF_PATH;
        candidate.onvifUrl = makeOnvifUrl(
            candidate.ip,
            candidate.onvifPort,
            candidate.onvifPath
        );

        const ServiceProbe candidateProbe = probeOnvif(candidate);

        if (!candidateProbe.protocolResponded) {
            continue;
        }

        camera.onvifPort = candidate.onvifPort;
        camera.onvifPath = candidate.onvifPath;
        camera.onvifUrl = candidate.onvifUrl;
        lastOnvifProbe = candidateProbe;
        invalidateSnapshotTarget();
        lastSnapshotResolveAttemptAtMs = 0;
        saveCameraTarget();

        Serial.printf(
            "Servico ONVIF confirmado na porta %u.\n",
            camera.onvifPort
        );
        return true;
    }

    lastOnvifProbe = currentProbe;
    return false;
}

bool postOnvifSoap(
    const String& serviceUrl,
    const char* action,
    const String& body,
    String& responseBody
)
{
    WiFiClient client;
    HTTPClient request;
    request.setConnectTimeout(TCP_CONNECT_TIMEOUT_MS);
    request.setTimeout(SNAPSHOT_HTTP_TIMEOUT_MS);
    request.setReuse(false);

    if (!request.begin(client, serviceUrl)) {
        return false;
    }

    String contentType =
        "application/soap+xml; charset=utf-8; action=\"";
    contentType += action;
    contentType += '\"';

    request.addHeader("Content-Type", contentType);
    request.setUserAgent(
        String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION
    );

    String envelope;
    envelope.reserve(body.length() + 256);
    envelope += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    envelope += "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" ";
    envelope += "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\">";
    envelope += "<s:Body>";
    envelope += body;
    envelope += "</s:Body></s:Envelope>";

    const int statusCode = request.POST(envelope);
    const int responseSize = request.getSize();

    if (
        statusCode < 200 ||
        statusCode >= 300 ||
        responseSize > static_cast<int>(MAX_ONVIF_RESPONSE_SIZE)
    ) {
        request.end();
        return false;
    }

    responseBody = request.getString();
    request.end();

    return
        !responseBody.isEmpty() &&
        responseBody.length() <= MAX_ONVIF_RESPONSE_SIZE;
}

bool resolveSnapshotTarget()
{
    lastSnapshotResolveAttemptAtMs = millis();
    invalidateSnapshotTarget();

    if (!camera.valid || WiFi.status() != WL_CONNECTED) {
        lastSnapshotResult = "Camera ou Wi-Fi indisponivel";
        return false;
    }

    if (
        !lastOnvifProbe.protocolResponded &&
        !resolveCameraOnvifEndpoint()
    ) {
        lastSnapshotResult = "Servico ONVIF nao localizado";
        return false;
    }

    lastSnapshotResolveAttemptAtMs = millis();

    const String mediaUrl = makeOnvifUrl(
        camera.ip,
        camera.onvifPort,
        "/onvif/media_service"
    );

    String profileToken;
    String profileName;

    {
        String profilesXml;

        if (!postOnvifSoap(
            mediaUrl,
            "http://www.onvif.org/ver10/media/wsdl/GetProfiles",
            "<trt:GetProfiles/>",
            profilesXml
        )) {
            lastSnapshotResult = "ONVIF nao retornou os perfis de midia";
            return false;
        }

        if (!selectSnapshotProfile(
            profilesXml,
            profileToken,
            profileName
        )) {
            lastSnapshotResult = "Nenhum perfil ONVIF compativel";
            return false;
        }
    }

    String snapshotRequest;
    snapshotRequest.reserve(profileToken.length() + 96);
    snapshotRequest += "<trt:GetSnapshotUri><trt:ProfileToken>";
    snapshotRequest += htmlEscape(profileToken);
    snapshotRequest += "</trt:ProfileToken></trt:GetSnapshotUri>";

    String snapshotXml;

    if (!postOnvifSoap(
        mediaUrl,
        "http://www.onvif.org/ver10/media/wsdl/GetSnapshotUri",
        snapshotRequest,
        snapshotXml
    )) {
        lastSnapshotResult = "ONVIF nao retornou o snapshot";
        return false;
    }

    String snapshotUrl;
    int ignoredPosition = 0;

    if (!extractLocalTag(
        snapshotXml,
        "Uri",
        0,
        snapshotUrl,
        ignoredPosition
    )) {
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
    // O host informado pelo equipamento nao e reutilizado. O proxy sempre
    // conecta ao mesmo IP que ja foi validado como a camera monitorada; apenas
    // porta, caminho e query do URI ONVIF sao aproveitados. Isso aceita cameras
    // que anunciam 0.0.0.0 ou outro endereco local sem abrir um redirecionamento
    // arbitrario pelo endpoint do ESP32.
    snapshotTarget.url = makeOnvifUrl(
        camera.ip,
        parsedSnapshot.port,
        parsedSnapshot.path
    );
    snapshotTarget.profileToken = profileToken;
    snapshotTarget.profileName = profileName;
    lastSnapshotResult =
        currentTimestamp() + " - perfil " + snapshotTarget.profileName;

    return true;
}

void sendSnapshotError(int statusCode, const char* message)
{
    snapshotErrors++;
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(
        statusCode,
        "application/json; charset=utf-8",
        String("{\"error\":\"") + message + "\"}"
    );
}

void serveCameraSnapshot()
{
    if (!snapshotTarget.valid) {
        if (
            lastSnapshotResolveAttemptAtMs != 0 &&
            millis() - lastSnapshotResolveAttemptAtMs <
                SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS
        ) {
            sendSnapshotError(503, "snapshot_temporarily_unavailable");
            return;
        }

        if (!resolveSnapshotTarget()) {
            sendSnapshotError(503, "snapshot_unavailable");
            return;
        }
    }

    WiFiClient cameraClient;
    HTTPClient request;
    request.setConnectTimeout(TCP_CONNECT_TIMEOUT_MS);
    request.setTimeout(SNAPSHOT_HTTP_TIMEOUT_MS);
    request.setReuse(false);
    request.setUserAgent(
        String("ESP32-Camera-Watchdog/") + FIRMWARE_VERSION
    );

    const char* snapshotHeaders[] = {"Content-Type"};
    request.collectHeaders(snapshotHeaders, 1);

    if (!request.begin(cameraClient, snapshotTarget.url)) {
        invalidateSnapshotTarget();
        sendSnapshotError(502, "camera_connection_failed");
        return;
    }

    const int statusCode = request.GET();
    String contentType = request.header("Content-Type");
    contentType.toLowerCase();
    const int contentLength = request.getSize();

    if (
        statusCode < 200 ||
        statusCode >= 300 ||
        contentType.indexOf("image/jpeg") < 0 ||
        contentLength <= 0 ||
        contentLength > static_cast<int>(MAX_SNAPSHOT_SIZE_BYTES)
    ) {
        request.end();
        invalidateSnapshotTarget();
        lastSnapshotResolveAttemptAtMs = 0;
        lastSnapshotResult =
            currentTimestamp() + " - falha HTTP " + String(statusCode);
        sendSnapshotError(502, "invalid_camera_response");
        return;
    }

    WiFiClient* source = request.getStreamPtr();

    if (
        source == nullptr ||
        (!source->connected() && source->available() == 0)
    ) {
        request.end();
        invalidateSnapshotTarget();
        sendSnapshotError(502, "camera_stream_unavailable");
        return;
    }

    httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    httpServer.sendHeader("Pragma", "no-cache");
    httpServer.sendHeader("X-Content-Type-Options", "nosniff");
    httpServer.setContentLength(static_cast<size_t>(contentLength));
    httpServer.send(200, "image/jpeg", "");

    WiFiClient browserClient = httpServer.client();
    uint8_t relayBuffer[SNAPSHOT_RELAY_BUFFER_SIZE];
    size_t remaining = static_cast<size_t>(contentLength);
    unsigned long lastDataAtMs = millis();

    while (remaining > 0 && browserClient.connected()) {
        updateCameraAlertLed();
        const size_t availableBytes = source->available();

        if (availableBytes > 0) {
            const size_t bytesToRead = min(
                remaining,
                min(availableBytes, sizeof(relayBuffer))
            );
            const size_t bytesRead = source->readBytes(
                relayBuffer,
                bytesToRead
            );

            if (bytesRead == 0) {
                break;
            }

            httpServer.sendContent(
                reinterpret_cast<const char*>(relayBuffer),
                bytesRead
            );
            remaining -= bytesRead;
            lastDataAtMs = millis();
            continue;
        }

        if (
            !source->connected() ||
            millis() - lastDataAtMs >= SNAPSHOT_HTTP_TIMEOUT_MS
        ) {
            break;
        }

        delay(1);
    }

    request.end();

    if (remaining == 0) {
        snapshotsServed++;
        lastSnapshotResult =
            currentTimestamp() + " - quadro entregue; perfil " +
            snapshotTarget.profileName;
    } else {
        snapshotErrors++;
        lastSnapshotResult = "Transferencia do snapshot interrompida";
    }
}

ServiceProbe probeRtsp(const IPAddress& ip, uint16_t port)
{
    ServiceProbe result;
    WiFiClient client;
    const unsigned long startedAt = millis();

    if (!client.connect(ip, port, TCP_CONNECT_TIMEOUT_MS)) {
        result.latencyMs = millis() - startedAt;
        return result;
    }

    result.tcpConnected = true;
    result.latencyMs = millis() - startedAt;

    client.print("OPTIONS rtsp://");
    client.print(ip.toString());
    client.print(':');
    client.print(port);
    client.print("/ RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: ESP32-Camera-Watchdog/");
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
    return result;
}

bool tcpPortOpen(
    const IPAddress& ip,
    uint16_t port,
    uint32_t timeoutMs
)
{
    WiFiClient client;
    const bool connected = client.connect(ip, port, timeoutMs);
    client.stop();
    return connected;
}

bool scanSubnetForCamera()
{
    subnetScanRuns++;
    lastSubnetScanAtMs = millis();
    lastEvent = currentTimestamp() + " - Varrendo a sub-rede por RTSP e ONVIF";
    Serial.println(lastEvent);

    const uint32_t local = ipToUint32(WiFi.localIP());
    const uint32_t mask = ipToUint32(WiFi.subnetMask());
    const uint32_t network = local & mask;
    const uint32_t broadcast = network | (~mask);

    if (broadcast <= network + 1) {
        lastScanResult = "Mascara de rede invalida para varredura";
        return false;
    }

    const uint32_t hostCount = broadcast - network - 1;

    if (hostCount > MAX_SCAN_HOSTS) {
        lastScanResult =
            "Rede possui " +
            String(hostCount) +
            " hosts; limite configurado=" +
            String(MAX_SCAN_HOSTS);

        Serial.println(lastScanResult);
        return false;
    }

    const IPAddress ownIp = WiFi.localIP();
    const IPAddress gateway = WiFi.gatewayIP();

    Serial.printf(
        "Varredura de %s ate %s (%lu hosts)\n",
        uint32ToIp(network + 1).toString().c_str(),
        uint32ToIp(broadcast - 1).toString().c_str(),
        static_cast<unsigned long>(hostCount)
    );

    for (uint32_t value = network + 1; value < broadcast; value++) {
        updateCameraAlertLed();
        const IPAddress candidateIp = uint32ToIp(value);

        if (candidateIp == ownIp || candidateIp == gateway) {
            continue;
        }

        httpServer.handleClient();

        if (!tcpPortOpen(
            candidateIp,
            CAMERA_RTSP_PORT,
            SCAN_CONNECT_TIMEOUT_MS
        )) {
            delay(1);
            continue;
        }

        Serial.printf(
            "Porta RTSP aberta em %s\n",
            candidateIp.toString().c_str()
        );

        for (size_t portIndex = 0;
             portIndex < ONVIF_PORT_CANDIDATE_COUNT;
             portIndex++) {
            const uint16_t onvifPort = ONVIF_PORT_CANDIDATES[portIndex];
            CameraTarget candidate;
            candidate.valid = true;
            candidate.ip = candidateIp;
            candidate.onvifPort = onvifPort;
            candidate.rtspPort = CAMERA_RTSP_PORT;
            candidate.onvifPath = DEFAULT_ONVIF_PATH;
            candidate.onvifUrl = makeOnvifUrl(
                candidate.ip,
                candidate.onvifPort,
                candidate.onvifPath
            );

            const ServiceProbe candidateProbe = probeOnvif(candidate);

            if (!candidateProbe.protocolResponded) {
                continue;
            }

            const String oldIp = camera.valid ? camera.ip.toString() : "";
            camera = candidate;
            camera.endpointUuid = "";
            camera.scopes = "Localizada por varredura RTSP/ONVIF";
            lastOnvifProbe = candidateProbe;
            invalidateSnapshotTarget();
            lastSnapshotResolveAttemptAtMs = 0;
            saveCameraTarget();

            lastScanResult =
                currentTimestamp() +
                " - camera candidata em " +
                camera.onvifUrl;

            Serial.println(lastScanResult);

            if (!oldIp.isEmpty() && oldIp != camera.ip.toString()) {
                sendIpChangeAlert(oldIp, camera.ip.toString());
            }

            return true;
        }
    }

    lastScanResult = currentTimestamp() + " - nenhuma camera localizada";
    Serial.println(lastScanResult);
    return false;
}

bool cameraResponded()
{
    if (!camera.valid) {
        return false;
    }

    lastOnvifProbe = probeOnvif(camera);
    httpServer.handleClient();
    lastRtspProbe = probeRtsp(camera.ip, camera.rtspPort);

    return lastOnvifProbe.tcpConnected || lastRtspProbe.tcpConnected;
}

void runHealthCheck()
{
    lastHealthCheckAtMs = millis();
    totalHealthChecks++;

    if (WiFi.status() != WL_CONNECTED) {
        failedHealthChecks++;
        consecutiveFailures++;
        consecutiveSuccesses = 0;
        setState(CameraState::Degraded, "ESP32 perdeu a conexao Wi-Fi");
        return;
    }

    if (!camera.valid) {
        discoverOnvifCamera();
    }

    bool online = cameraResponded();

    if (!online && cameraState != CameraState::Offline) {
        setState(
            CameraState::Degraded,
            "Camera nao respondeu; tentando recuperar"
        );
        updateCameraAlertLed();
    }

    if (
        !online &&
        consecutiveFailures + 1 >= FAILURES_BEFORE_REDISCOVERY &&
        millis() - lastRediscoveryAtMs >= REDISCOVERY_INTERVAL_MS
    ) {
        const bool rediscovered = discoverOnvifCamera();

        if (rediscovered) {
            online = cameraResponded();
        }
    }

    if (
        !online &&
        ENABLE_AUTOMATIC_SUBNET_SCAN &&
        (
            lastSubnetScanAtMs == 0 ||
            millis() - lastSubnetScanAtMs >= SUBNET_SCAN_INTERVAL_MS
        )
    ) {
        const bool foundByScan = scanSubnetForCamera();

        if (foundByScan) {
            online = cameraResponded();
        }
    }

    if (online) {
        successfulHealthChecks++;
        consecutiveFailures = 0;
        consecutiveSuccesses++;

        Serial.printf(
            "CAMERA OK: %s | ONVIF tcp=%d http=%d code=%d | RTSP tcp=%d protocolo=%d code=%d\n",
            camera.ip.toString().c_str(),
            lastOnvifProbe.tcpConnected,
            lastOnvifProbe.protocolResponded,
            lastOnvifProbe.statusCode,
            lastRtspProbe.tcpConnected,
            lastRtspProbe.protocolResponded,
            lastRtspProbe.statusCode
        );

        if (
            cameraState == CameraState::Offline &&
            consecutiveSuccesses >= SUCCESSES_BEFORE_RECOVERY
        ) {
            setState(CameraState::Online, "Camera recuperada");

            if (offlineAlertSent) {
                pendingRecoveryEmail = true;
            }

            offlineAlertSent = false;
        } else if (cameraState != CameraState::Offline) {
            setState(CameraState::Online, "Camera respondeu ao teste");
        }

        return;
    }

    failedHealthChecks++;
    consecutiveSuccesses = 0;

    if (consecutiveFailures < 255) {
        consecutiveFailures++;
    }

    Serial.printf(
        "CAMERA FALHOU: tentativas consecutivas=%u | IP=%s\n",
        consecutiveFailures,
        camera.valid ? camera.ip.toString().c_str() : "desconhecido"
    );

    if (consecutiveFailures < FAILURES_BEFORE_OFFLINE) {
        setState(CameraState::Degraded, "Camera nao respondeu; confirmando falha");
        return;
    }

    if (millis() - bootAtMs < STARTUP_GRACE_MS) {
        setState(CameraState::Degraded, "Falha dentro do periodo de tolerancia inicial");
        return;
    }

    if (cameraState != CameraState::Offline) {
        offlineStartedAtMs = millis();
        offlineAlertSent = false;
        pendingOfflineEmail = true;
        setState(CameraState::Offline, "Camera confirmada como indisponivel");
    }
}

String buildStatusJson()
{
    String json;
    json.reserve(2200);

    json += "{";
    json += "\"firmwareVersion\":\"";
    json += FIRMWARE_VERSION;
    json += "\",";
    json += "\"state\":\"";
    json += stateName(cameraState);
    json += "\",";
    json += "\"lastEvent\":\"";
    json += jsonEscape(lastEvent);
    json += "\",";
    json += "\"uptimeSeconds\":";
    json += millis() / 1000UL;
    json += ",";
    json += "\"stateSinceSeconds\":";
    json += (millis() - lastStateChangeAtMs) / 1000UL;
    json += ",";
    json += "\"freeHeapBytes\":";
    json += ESP.getFreeHeap();
    json += ",";
    json += "\"minimumFreeHeapBytes\":";
    json += ESP.getMinFreeHeap();
    json += ",";

    json += "\"bluetoothMemory\":{";
    json += "\"configured\":";
    json += RELEASE_BLUETOOTH_MEMORY ? "true" : "false";
    json += ",\"attempted\":";
    json += bluetoothMemoryReleaseAttempted ? "true" : "false";
    json += ",\"result\":";
    json += static_cast<int>(bluetoothMemoryReleaseResult);
    json += ",\"controllerStatusBefore\":";
    json += static_cast<int>(bluetoothControllerStatusBeforeRelease);
    json += ",\"startedBefore\":";
    json += bluetoothStartedBeforeRelease ? "true" : "false";
    json += ",\"heapBeforeBytes\":";
    json += heapBeforeBluetoothReleaseBytes;
    json += ",\"heapAfterBytes\":";
    json += heapAfterBluetoothReleaseBytes;
    json += ",\"releasedBytes\":";
    json += bluetoothMemoryReleasedBytes;
    json += "},";

    json += "\"wifi\":{";
    json += "\"connected\":";
    json += WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"ssid\":\"";
    json += jsonEscape(WiFi.SSID());
    json += "\",\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\",\"gateway\":\"";
    json += WiFi.gatewayIP().toString();
    json += "\",\"rssi\":";
    json += WiFi.RSSI();
    json += "},";

    json += "\"camera\":{";
    json += "\"name\":\"";
    json += jsonEscape(CAMERA_NAME);
    json += "\",\"known\":";
    json += camera.valid ? "true" : "false";
    json += ",\"ip\":\"";
    json += camera.valid ? camera.ip.toString() : String("");
    json += "\",\"onvifUrl\":\"";
    json += jsonEscape(camera.onvifUrl);
    json += "\",\"uuid\":\"";
    json += jsonEscape(camera.endpointUuid);
    json += "\",\"scopes\":\"";
    json += jsonEscape(camera.scopes);
    json += "\",\"rtspPort\":";
    json += camera.rtspPort;
    json += "},";

    json += "\"probes\":{";
    json += "\"onvifTcp\":";
    json += lastOnvifProbe.tcpConnected ? "true" : "false";
    json += ",\"onvifHttp\":";
    json += lastOnvifProbe.protocolResponded ? "true" : "false";
    json += ",\"onvifStatus\":";
    json += lastOnvifProbe.statusCode;
    json += ",\"onvifLatencyMs\":";
    json += lastOnvifProbe.latencyMs;
    json += ",\"rtspTcp\":";
    json += lastRtspProbe.tcpConnected ? "true" : "false";
    json += ",\"rtspProtocol\":";
    json += lastRtspProbe.protocolResponded ? "true" : "false";
    json += ",\"rtspStatus\":";
    json += lastRtspProbe.statusCode;
    json += ",\"rtspLatencyMs\":";
    json += lastRtspProbe.latencyMs;
    json += "},";

    json += "\"snapshot\":{";
    json += "\"ready\":";
    json += snapshotTarget.valid ? "true" : "false";
    json += ",\"profile\":\"";
    json += jsonEscape(snapshotTarget.profileName);
    json += "\",\"served\":";
    json += snapshotsServed;
    json += ",\"errors\":";
    json += snapshotErrors;
    json += ",\"lastResult\":\"";
    json += jsonEscape(lastSnapshotResult);
    json += "\"},";

    json += "\"counters\":{";
    json += "\"totalChecks\":";
    json += totalHealthChecks;
    json += ",\"successfulChecks\":";
    json += successfulHealthChecks;
    json += ",\"failedChecks\":";
    json += failedHealthChecks;
    json += ",\"consecutiveFailures\":";
    json += consecutiveFailures;
    json += ",\"consecutiveSuccesses\":";
    json += consecutiveSuccesses;
    json += ",\"discoveryRuns\":";
    json += discoveryRuns;
    json += ",\"subnetScanRuns\":";
    json += subnetScanRuns;
    json += ",\"emailsSent\":";
    json += emailsSent;
    json += ",\"emailErrors\":";
    json += emailErrors;
    json += "},";

    json += "\"lastDiscovery\":\"";
    json += jsonEscape(lastDiscoveryResult);
    json += "\",\"lastSubnetScan\":\"";
    json += jsonEscape(lastScanResult);
    json += "\",\"lastEmailResult\":\"";
    json += jsonEscape(lastEmailResult);
    json += "\",\"resendConfigured\":";
    json += resendConfigured() ? "true" : "false";
    json += "}";

    return json;
}

void configureHttpServer()
{
    httpServer.on("/", HTTP_GET, [] {
        // Servido com send_P para transmitir direto da flash. A sobrecarga
        // send(int, const char*, const char*) copiaria a pagina inteira para a
        // heap antes de escrever no socket.
        httpServer.send_P(200, "text/html; charset=utf-8", DASHBOARD_PAGE);
    });

    httpServer.on("/api/status", HTTP_GET, [] {
        httpServer.send(
            200,
            "application/json; charset=utf-8",
            buildStatusJson()
        );
    });

    httpServer.on("/api/camera/snapshot", HTTP_GET, [] {
        serveCameraSnapshot();
    });

    httpServer.on("/health", HTTP_GET, [] {
        httpServer.send(200, "text/plain", "ok");
    });

    httpServer.onNotFound([] {
        httpServer.send(404, "application/json", "{\"error\":\"not_found\"}");
    });

    httpServer.begin();
    Serial.printf("Painel HTTP iniciado na porta %u.\n", HTTP_SERVER_PORT);
}

bool connectToWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);

    Serial.printf("Conectando a rede \"%s\"", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long startedAt = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS
    ) {
        updateCameraAlertLed();
        Serial.print('.');
        delay(500);
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Falha ao conectar ao Wi-Fi.");
        return false;
    }

    Serial.printf("Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Mascara: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

    configTime(0, 0, "pool.ntp.org", "time.google.com");

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_SERVER_PORT);
        Serial.printf("mDNS: http://%s.local\n", HOSTNAME);
    }

    return true;
}

void processPendingEmails()
{
    if (
        millis() - lastEmailAttemptAtMs < EMAIL_RETRY_INTERVAL_MS &&
        lastEmailAttemptAtMs != 0
    ) {
        return;
    }

    if (pendingOfflineEmail) {
        sendOfflineAlert();
        return;
    }

    if (pendingRecoveryEmail) {
        sendRecoveryAlert();
    }
}

void maintainWiFi()
{
    static unsigned long lastReconnectAttemptAtMs = 0;

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    if (millis() - lastReconnectAttemptAtMs < 10000UL) {
        return;
    }

    lastReconnectAttemptAtMs = millis();
    Serial.println("Tentando reconectar o ESP32 ao Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    configureCameraAlertLed();
    updateCameraAlertLed();
    delay(800);

    bootAtMs = millis();
    lastStateChangeAtMs = bootAtMs;

    Serial.println();
    Serial.println("ESP32 Camera Watchdog");
    Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
    Serial.printf("Chip: %s rev. %u\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("Flash: %u MB\n", ESP.getFlashChipSize() / 1024U / 1024U);
    releaseUnusedBluetoothMemory();
    Serial.printf("Heap inicial disponivel: %u bytes\n", ESP.getFreeHeap());

    if (!connectToWiFi()) {
        setState(CameraState::Degraded, "ESP32 sem Wi-Fi");
        return;
    }

    configureHttpServer();

    if (!loadCameraTarget()) {
        loadCameraIpHint();
    }

    if (!camera.valid) {
        discoverOnvifCamera();
    }

    if (camera.valid && resolveCameraOnvifEndpoint()) {
        resolveSnapshotTarget();
    }

    Serial.printf("Painel: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Painel mDNS: http://%s.local\n", HOSTNAME);

    runHealthCheck();
}

void loop()
{
    updateCameraAlertLed();
    httpServer.handleClient();
    maintainWiFi();
    processPendingEmails();

    if (millis() - lastHealthCheckAtMs >= HEALTH_CHECK_INTERVAL_MS) {
        runHealthCheck();
    }

    updateCameraAlertLed();
    delay(2);
}
