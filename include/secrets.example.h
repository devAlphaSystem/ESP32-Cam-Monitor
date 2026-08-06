#pragma once

// Wi-Fi 2,4 GHz ao qual o ESP32 ficara conectado.
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";

// Usuario administrador da camera/ONVIF. Deixe ambos vazios se a camera nao
// exigir autenticacao. O firmware tenta WS-Security, HTTP Basic e HTTP Digest.
// O uso de macros mantem compatibilidade com arquivos secrets.h antigos.
#define CAMERA_ONVIF_USERNAME_VALUE ""
#define CAMERA_ONVIF_PASSWORD_VALUE ""

// Resend: crie uma chave de API com permissao de envio.
constexpr char RESEND_API_KEY[] = "";

// Use um endereco de um dominio verificado no Resend.
constexpr char RESEND_FROM[] = "Camera Watchdog <watchdog@example.com>";

// Destinatario dos alertas.
constexpr char ALERT_EMAIL[] = "recipient@example.com";
