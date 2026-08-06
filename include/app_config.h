#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Identificacao e descoberta da camera
// -----------------------------------------------------------------------------

// Nome exibido no painel e nos e-mails.
constexpr char CAMERA_NAME[] = "Camera de seguranca";

// Opcional: trecho de UUID, XAddr ou Scope retornado pelo ONVIF.
// Deixe vazio para selecionar a melhor camera encontrada.
constexpr char CAMERA_MATCH[] = "";

// Opcional: IP inicial conhecido. O projeto continua tentando WS-Discovery e
// atualiza o endereco salvo caso a camera mude de IP.
constexpr char CAMERA_IP_HINT[] = "";

// Perfil ONVIF preferido para snapshots JPEG. Se nenhum perfil contiver este
// trecho no nome, o primeiro perfil retornado pela camera sera usado.
constexpr char CAMERA_SNAPSHOT_PROFILE_MATCH[] = "sub";

constexpr uint16_t CAMERA_RTSP_PORT = 554;
constexpr char DEFAULT_ONVIF_PATH[] = "/onvif/device_service";
constexpr char DEFAULT_ONVIF_MEDIA_PATH[] = "/onvif/media_service";

// Portas ONVIF comuns. A varredura de sub-rede so testa estas portas depois de
// encontrar a porta RTSP aberta no host candidato.
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

// -----------------------------------------------------------------------------
// Monitoramento e recuperacao automatica
// -----------------------------------------------------------------------------

// Cada verificacao testa ONVIF, RTSP e um JPEG completo. Trinta segundos evita
// sobrecarregar cameras simples e ainda detecta uma falha em poucos minutos.
constexpr unsigned long HEALTH_CHECK_INTERVAL_MS = 30000UL;
constexpr unsigned long STARTUP_GRACE_MS = 45000UL;

constexpr uint8_t FAILURES_BEFORE_REDISCOVERY = 2;
constexpr uint8_t FAILURES_BEFORE_OFFLINE = 3;
constexpr uint8_t SUCCESSES_BEFORE_RECOVERY = 2;

// Uma falha de video e confirmada somente depois de varios JPEGs invalidos.
constexpr uint8_t VIDEO_FAILURES_BEFORE_ALERT = 3;
constexpr uint8_t VIDEO_FAILURES_BEFORE_REBOOT = 3;

// Tenta o comando ONVIF SystemReboot quando a camera ainda responde, mas deixou
// de entregar imagem. Desative caso prefira apenas receber o alerta.
constexpr bool ENABLE_ONVIF_AUTO_REBOOT = true;
constexpr unsigned long ONVIF_REBOOT_GRACE_MS = 90000UL;
constexpr unsigned long ONVIF_REBOOT_COOLDOWN_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long ONVIF_REBOOT_WINDOW_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint8_t MAX_ONVIF_REBOOT_ATTEMPTS_PER_WINDOW = 3;

// -----------------------------------------------------------------------------
// Descoberta, rede e e-mail
// -----------------------------------------------------------------------------

constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000UL;
constexpr unsigned long DISCOVERY_TIMEOUT_MS = 3500UL;
constexpr unsigned long REDISCOVERY_INTERVAL_MS = 60000UL;
constexpr unsigned long SUBNET_SCAN_INTERVAL_MS = 300000UL;
constexpr unsigned long EMAIL_RETRY_INTERVAL_MS = 60000UL;

constexpr bool ENABLE_AUTOMATIC_SUBNET_SCAN = true;
constexpr bool EMAIL_ON_IP_CHANGE = true;
constexpr size_t MAX_SCAN_HOSTS = 254;

// -----------------------------------------------------------------------------
// Timeouts e limites de memoria
// -----------------------------------------------------------------------------

constexpr uint32_t TCP_CONNECT_TIMEOUT_MS = 900UL;
constexpr uint32_t SERVICE_RESPONSE_TIMEOUT_MS = 1500UL;
constexpr uint32_t SCAN_CONNECT_TIMEOUT_MS = 90UL;
constexpr uint32_t SNAPSHOT_HTTP_TIMEOUT_MS = 7000UL;
constexpr unsigned long SNAPSHOT_RESOLVE_RETRY_INTERVAL_MS = 30000UL;

constexpr size_t MAX_ONVIF_RESPONSE_SIZE = 32768;
constexpr size_t MAX_SNAPSHOT_SIZE_BYTES = 512UL * 1024UL;
constexpr size_t MIN_VALID_SNAPSHOT_SIZE_BYTES = 512;
constexpr size_t SNAPSHOT_PROBE_BUFFER_SIZE = 512;

// Buffer maior reduz o numero de operacoes durante o proxy da previa.
constexpr size_t SNAPSHOT_RELAY_BUFFER_SIZE = 2048;

// Depois de duas respostas de imagem ruins, resolve novamente GetSnapshotUri.
constexpr uint8_t SNAPSHOT_FAILURES_BEFORE_RERESOLVE = 2;

// -----------------------------------------------------------------------------
// Hardware e servidor local
// -----------------------------------------------------------------------------

// LED integrado usado como alerta local. Nesta placa, GPIO 2 acende em HIGH.
constexpr uint8_t CAMERA_ALERT_LED_PIN = 2;
constexpr unsigned long CAMERA_ALERT_LED_BLINK_INTERVAL_MS = 1000UL;

// Devolve ao heap a memoria reservada para Bluetooth Classic e BLE.
// Use 0 se o firmware passar a usar Bluetooth no futuro.
#define RELEASE_BLUETOOTH_MEMORY 1

constexpr uint16_t HTTP_SERVER_PORT = 80;
constexpr uint16_t WS_DISCOVERY_PORT = 3702;
constexpr uint16_t WS_DISCOVERY_LOCAL_PORT = 37020;
constexpr size_t WS_DISCOVERY_BUFFER_SIZE = 4096;
constexpr size_t MAX_DISCOVERED_CAMERAS = 8;
