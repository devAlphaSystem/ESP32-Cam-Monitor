#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "secrets.h"

namespace {

constexpr uint8_t BLUE_LED_PIN = 2;
constexpr uint32_t LED_BLINK_INTERVAL_MS = 500UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000UL;
constexpr char HOSTNAME[] = "esp32-recovery";

WebServer server(80);
bool ledOn = false;
bool serverStarted = false;
uint32_t lastLedToggleAtMs = 0;
uint32_t lastReconnectAttemptAtMs = 0;

String formatBytes(uint32_t bytes)
{
    if (bytes >= 1024UL * 1024UL) {
        return String(static_cast<double>(bytes) / (1024.0 * 1024.0), 2) + " MB";
    }

    if (bytes >= 1024UL) {
        return String(static_cast<double>(bytes) / 1024.0, 1) + " KB";
    }

    return String(bytes) + " B";
}

String formatUptime(uint32_t uptimeSeconds)
{
    const uint32_t days = uptimeSeconds / 86400UL;
    const uint8_t hours = static_cast<uint8_t>((uptimeSeconds % 86400UL) / 3600UL);
    const uint8_t minutes = static_cast<uint8_t>((uptimeSeconds % 3600UL) / 60UL);
    const uint8_t seconds = static_cast<uint8_t>(uptimeSeconds % 60UL);

    char value[32];
    snprintf(value, sizeof(value), "%lu d %02u:%02u:%02u", static_cast<unsigned long>(days), hours, minutes, seconds);
    return String(value);
}

const char* resetReasonName(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:
            return "Power-on";
        case ESP_RST_EXT:
            return "External pin";
        case ESP_RST_SW:
            return "Software";
        case ESP_RST_PANIC:
            return "Panic";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "Task watchdog";
        case ESP_RST_WDT:
            return "Watchdog";
        case ESP_RST_DEEPSLEEP:
            return "Deep sleep wake-up";
        case ESP_RST_BROWNOUT:
            return "Brownout";
        case ESP_RST_SDIO:
            return "SDIO";
        case ESP_RST_UNKNOWN:
            return "Unknown";
    }

    return "Unknown";
}

void appendRow(String& html, const char* label, const String& value)
{
    html += "<tr><th>";
    html += label;
    html += "</th><td>";
    html += value;
    html += "</td></tr>";
}

String buildStatusPage()
{
    String html;
    html.reserve(5200);
    html += F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">" "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" "<meta http-equiv=\"refresh\" content=\"3\"><title>ESP32 Recovery</title>" "<style>body{margin:0;background:#0b1220;color:#dbeafe;font:16px system-ui,sans-serif}" ".box{max-width:760px;margin:32px auto;padding:0 18px}.card{background:#111c30;border:1px solid #29405f;border-radius:14px;padding:22px;box-shadow:0 12px 35px #0005}" "h1{margin:0 0 6px;font-size:26px}.ok{color:#67e8f9;margin:0 0 20px}" "table{width:100%;border-collapse:collapse}th,td{padding:10px 8px;border-bottom:1px solid #263852;text-align:left}th{width:44%;color:#93c5fd;font-weight:600}tr:last-child th,tr:last-child td{border-bottom:0}" "</style></head><body><main class=\"box\"><section class=\"card\">" "<h1>ESP32 operational</h1><p class=\"ok\">Minimal diagnostic firmware running</p><table>");

    appendRow(html, "Chip model", ESP.getChipModel());
    appendRow(html, "Revision", String(ESP.getChipRevision()));
    appendRow(html, "Cores", String(ESP.getChipCores()));
    appendRow(html, "CPU frequency", String(ESP.getCpuFreqMHz()) + " MHz");
    appendRow(html, "ESP-IDF SDK", ESP.getSdkVersion());
    appendRow(html, "Installed flash", formatBytes(ESP.getFlashChipSize()));
    appendRow(html, "Flash speed", String(ESP.getFlashChipSpeed() / 1000000UL) + " MHz");
    appendRow(html, "Flashed firmware", formatBytes(ESP.getSketchSize()));
    appendRow(html, "Free application space", formatBytes(ESP.getFreeSketchSpace()));
    appendRow(html, "Total heap", formatBytes(ESP.getHeapSize()));
    appendRow(html, "Free heap", formatBytes(ESP.getFreeHeap()));
    appendRow(html, "Lowest free heap", formatBytes(ESP.getMinFreeHeap()));
    appendRow(html, "Largest heap block", formatBytes(ESP.getMaxAllocHeap()));
    appendRow(html, "Total PSRAM", ESP.getPsramSize() > 0 ? formatBytes(ESP.getPsramSize()) : String("Not installed"));
    appendRow(html, "Free PSRAM", ESP.getPsramSize() > 0 ? formatBytes(ESP.getFreePsram()) : String("Not applicable"));
    appendRow(html, "Last reset reason", resetReasonName(esp_reset_reason()));
    appendRow(html, "Uptime", formatUptime(millis() / 1000UL));
    appendRow(html, "IP address", WiFi.localIP().toString());
    appendRow(html, "Gateway", WiFi.gatewayIP().toString());
    appendRow(html, "Wi-Fi signal", String(WiFi.RSSI()) + " dBm");
    appendRow(html, "MAC address", WiFi.macAddress());

    html += F("</table></section></main></body></html>");
    return html;
}

void updateBlueLed()
{
    const uint32_t now = millis();

    if (now - lastLedToggleAtMs < LED_BLINK_INTERVAL_MS) {
        return;
    }

    lastLedToggleAtMs = now;
    ledOn = !ledOn;
    digitalWrite(BLUE_LED_PIN, ledOn ? HIGH : LOW);
}

void startServer()
{
    if (serverStarted || WiFi.status() != WL_CONNECTED) {
        return;
    }

    server.begin();
    serverStarted = true;

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
    }

    Serial.printf("Server: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("Local address: http://%s.local/\n", HOSTNAME);
}

void beginWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastReconnectAttemptAtMs = millis();
}

void maintainWiFi()
{
    if (WiFi.status() == WL_CONNECTED) {
        startServer();
        server.handleClient();
        return;
    }

    const uint32_t now = millis();

    if (now - lastReconnectAttemptAtMs < WIFI_RECONNECT_INTERVAL_MS) {
        return;
    }

    lastReconnectAttemptAtMs = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

}

void setup()
{
    Serial.begin(115200);
    pinMode(BLUE_LED_PIN, OUTPUT);
    digitalWrite(BLUE_LED_PIN, LOW);

    server.on("/", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/html; charset=utf-8", buildStatusPage());
    });
    server.on("/health", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/plain; charset=utf-8", "OK");
    });
    server.onNotFound([]() {
        server.send(404, "text/plain; charset=utf-8", "Not found");
    });

    beginWiFi();
    const uint32_t connectStartedAtMs = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - connectStartedAtMs < WIFI_CONNECT_TIMEOUT_MS) {
        updateBlueLed();
        delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
        startServer();
    } else {
        Serial.println("Wi-Fi unavailable. Retries will happen automatically.");
    }
}

void loop()
{
    updateBlueLed();
    maintainWiFi();
    delay(2);
}
