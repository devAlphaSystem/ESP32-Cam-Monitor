#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include "log_http.h"
#include "log_reader.h"
#include "secrets.h"

namespace {

constexpr uint8_t BLUE_LED_PIN = 2;
constexpr uint32_t LED_BLINK_INTERVAL_MS = 500UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 15000UL;
constexpr char HOSTNAME[] = "esp32-log-reader";

WebServer server(80);
bool ledOn = false;
bool serverStarted = false;
bool accessPointStarted = false;
uint32_t lastLedToggleAtMs = 0;
uint32_t lastReconnectAttemptAtMs = 0;
char accessPointName[32] = {0};
char accessPointPassword[24] = {0};

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

void printReaderStatus()
{
    const LogReader::Status& status = LogReader::status();
    Serial.printf("Logs partition: address 0x%06lX, size %lu bytes.\n", static_cast<unsigned long>(status.partitionAddress), static_cast<unsigned long>(status.partitionSize));

    if (status.mountSkippedAfterReset) {
        Serial.println("Mount skipped: the previous restart happened during the mount attempt.");
    } else if (!status.mounted) {
        Serial.printf("Read-only mount unavailable: %s (%ld).\n", esp_err_to_name(status.mountError), static_cast<long>(status.mountError));
    } else if (!status.fileFound) {
        Serial.println("Partition mounted, but /events.bin was not found.");
    } else {
        Serial.printf("File examined: %lu slots, %lu valid, %lu invalid, %lu with unknown schema.\n", static_cast<unsigned long>(status.slotCount), static_cast<unsigned long>(status.validCount), static_cast<unsigned long>(status.invalidCount), static_cast<unsigned long>(status.unknownSchemaCount));
    }

    if (LogReader::partition() != nullptr) {
        Serial.println("RAW export is available even without a LittleFS mount.");
    }
}

void beginStation()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastReconnectAttemptAtMs = millis();
}

void startFallbackAccessPoint()
{
    if (accessPointStarted) {
        return;
    }

    const uint64_t chipId = ESP.getEfuseMac();
    snprintf(accessPointName, sizeof(accessPointName), "ESP32-LogReader-%06lX", static_cast<unsigned long>(chipId & 0xFFFFFFULL));
    snprintf(accessPointPassword, sizeof(accessPointPassword), "Logs-%06lX", static_cast<unsigned long>(chipId & 0xFFFFFFULL));

    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAP(accessPointName, accessPointPassword)) {
        Serial.println("Could not start the recovery access point.");
        return;
    }

    accessPointStarted = true;
    Serial.printf("Access point: %s\n", accessPointName);
    Serial.printf("Access point password: %s (Logs- followed by the name suffix).\n", accessPointPassword);
    Serial.printf("Server on the access point: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void startServer()
{
    if (serverStarted) {
        return;
    }

    server.begin();
    serverStarted = true;

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("Local address: http://%s.local/\n", HOSTNAME);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Server on the local network: http://%s/\n", WiFi.localIP().toString().c_str());
    }
}

void maintainNetwork()
{
    if (WiFi.status() == WL_CONNECTED) {
        server.handleClient();
        return;
    }

    const uint32_t now = millis();

    if (now - lastReconnectAttemptAtMs >= WIFI_RECONNECT_INTERVAL_MS) {
        lastReconnectAttemptAtMs = now;
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    if (!accessPointStarted) {
        startFallbackAccessPoint();
    }

    server.handleClient();
}

}

void setup()
{
    Serial.begin(115200);
    pinMode(BLUE_LED_PIN, OUTPUT);
    digitalWrite(BLUE_LED_PIN, LOW);

    LogReader::begin();
    printReaderStatus();
    LogHttp::configure(server);
    beginStation();

    const uint32_t connectionStartedAtMs = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - connectionStartedAtMs < WIFI_CONNECT_TIMEOUT_MS) {
        updateBlueLed();
        delay(10);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Configured Wi-Fi network unavailable. Starting recovery access.");
        startFallbackAccessPoint();
    }

    startServer();
}

void loop()
{
    updateBlueLed();
    maintainNetwork();
    delay(2);
}
