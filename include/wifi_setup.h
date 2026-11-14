#pragma once

#include <Arduino.h>

namespace CameraWatchdog {

constexpr size_t WIFI_SSID_MAX_LENGTH = 32;
constexpr size_t WIFI_PASSWORD_MAX_LENGTH = 63;
constexpr size_t WIFI_CREDENTIAL_CANDIDATES_MAX = 2;

struct WifiCredential {
    char ssid[WIFI_SSID_MAX_LENGTH + 1];
    char password[WIFI_PASSWORD_MAX_LENGTH + 1];
};

void wifiSetupBegin();

bool wifiSetupConnectedWithStoredCredential();

size_t wifiCredentialCandidates(WifiCredential* candidates);

void wifiSetupLoop();

bool wifiSetupAccessPointActive();

bool wifiSetupHoldsStation();

bool wifiSetupHandleNotFound();

void wifiSetupForgetStored();

}
