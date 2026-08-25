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
    Serial.printf("Particao logs: endereco 0x%06lX, tamanho %lu bytes.\n", static_cast<unsigned long>(status.partitionAddress), static_cast<unsigned long>(status.partitionSize));

    if (status.mountSkippedAfterReset) {
        Serial.println("Montagem ignorada: o reinicio anterior ocorreu durante a tentativa de montagem.");
    } else if (!status.mounted) {
        Serial.printf("Montagem somente leitura indisponivel: %s (%ld).\n", esp_err_to_name(status.mountError), static_cast<long>(status.mountError));
    } else if (!status.fileFound) {
        Serial.println("Particao montada, mas /events.bin nao foi encontrado.");
    } else {
        Serial.printf("Arquivo examinado: %lu slots, %lu validos, %lu invalidos, %lu com schema desconhecido.\n", static_cast<unsigned long>(status.slotCount), static_cast<unsigned long>(status.validCount), static_cast<unsigned long>(status.invalidCount), static_cast<unsigned long>(status.unknownSchemaCount));
    }

    if (LogReader::partition() != nullptr) {
        Serial.println("A exportacao RAW esta disponivel mesmo sem montagem do LittleFS.");
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
        Serial.println("Nao foi possivel iniciar o ponto de acesso de recuperacao.");
        return;
    }

    accessPointStarted = true;
    Serial.printf("Ponto de acesso: %s\n", accessPointName);
    Serial.printf("Senha do ponto de acesso: %s (Logs- seguido pelo sufixo do nome).\n", accessPointPassword);
    Serial.printf("Servidor no ponto de acesso: http://%s/\n", WiFi.softAPIP().toString().c_str());
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
        Serial.printf("Endereco local: http://%s.local/\n", HOSTNAME);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Servidor na rede local: http://%s/\n", WiFi.localIP().toString().c_str());
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
        Serial.println("Rede Wi-Fi configurada indisponivel; iniciando acesso de recuperacao.");
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
