#pragma once

#include <Arduino.h>

// Nome exibido no painel e nos e-mails.
constexpr char CAMERA_NAME[] = "Camera de seguranca";

// Opcional: trecho de UUID, XAddr ou Scope retornado pelo ONVIF.
// Deixe vazio para selecionar a primeira camera encontrada.
constexpr char CAMERA_MATCH[] = "";

// Opcional: IP inicial conhecido. O projeto continua tentando WS-Discovery
// e atualiza o IP se a camera mudar de endereco.
constexpr char CAMERA_IP_HINT[] = "";

// Perfil ONVIF preferido para a previa JPEG. Se nenhum perfil contiver este
// trecho no nome, o primeiro perfil retornado pela camera sera usado.
constexpr char CAMERA_SNAPSHOT_PROFILE_MATCH[] = "sub";

// LED integrado usado como alerta local. Nesta ESP32, GPIO 2 acende em HIGH.
constexpr uint8_t CAMERA_ALERT_LED_PIN = 2;
constexpr unsigned long CAMERA_ALERT_LED_BLINK_INTERVAL_MS = 1000UL;

// Devolve ao heap a memoria reservada para Bluetooth Classic e BLE.
// Use 0 se o firmware passar a usar Bluetooth no futuro.
#define RELEASE_BLUETOOTH_MEMORY 1

constexpr uint16_t CAMERA_RTSP_PORT = 554;
constexpr char DEFAULT_ONVIF_PATH[] = "/onvif/device_service";

// Portas comuns usadas por cameras ONVIF. A varredura de sub-rede somente
// testa estas portas depois de encontrar a porta RTSP aberta.
constexpr uint16_t ONVIF_PORT_CANDIDATES[] = {
    80,
    8899,
    8000,
    8080,
    2020
};

constexpr size_t ONVIF_PORT_CANDIDATE_COUNT =
    sizeof(ONVIF_PORT_CANDIDATES) /
    sizeof(ONVIF_PORT_CANDIDATES[0]);

constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000UL;
constexpr unsigned long HEALTH_CHECK_INTERVAL_MS = 10000UL;
constexpr unsigned long STARTUP_GRACE_MS = 45000UL;
constexpr unsigned long DISCOVERY_TIMEOUT_MS = 3500UL;
constexpr unsigned long REDISCOVERY_INTERVAL_MS = 60000UL;
constexpr unsigned long SUBNET_SCAN_INTERVAL_MS = 300000UL;
constexpr unsigned long EMAIL_RETRY_INTERVAL_MS = 60000UL;

constexpr uint8_t FAILURES_BEFORE_REDISCOVERY = 3;
constexpr uint8_t FAILURES_BEFORE_OFFLINE = 6;
constexpr uint8_t SUCCESSES_BEFORE_RECOVERY = 2;

constexpr uint32_t TCP_CONNECT_TIMEOUT_MS = 900UL;
constexpr uint32_t SERVICE_RESPONSE_TIMEOUT_MS = 1200UL;
constexpr uint32_t SCAN_CONNECT_TIMEOUT_MS = 90UL;
constexpr uint32_t SNAPSHOT_HTTP_TIMEOUT_MS = 6000UL;
constexpr unsigned long SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS = 30000UL;
constexpr size_t MAX_ONVIF_RESPONSE_SIZE = 32768;
// Buffer maior = menos iteracoes de leitura/escrita por quadro, liberando o
// loop principal mais rapido durante o relay do snapshot.
constexpr size_t SNAPSHOT_RELAY_BUFFER_SIZE = 2048;
constexpr size_t MAX_SNAPSHOT_SIZE_BYTES = 512 * 1024;

constexpr bool ENABLE_AUTOMATIC_SUBNET_SCAN = true;
constexpr bool EMAIL_ON_IP_CHANGE = true;
constexpr size_t MAX_SCAN_HOSTS = 254;

constexpr uint16_t HTTP_SERVER_PORT = 80;
constexpr uint16_t WS_DISCOVERY_PORT = 3702;
constexpr uint16_t WS_DISCOVERY_LOCAL_PORT = 37020;
constexpr size_t WS_DISCOVERY_BUFFER_SIZE = 4096;
constexpr size_t MAX_DISCOVERED_CAMERAS = 8;
