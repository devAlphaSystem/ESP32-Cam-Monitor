#include "firmware_internal.h"
#include "wifi_setup.h"

#include <DNSServer.h>
#include <esp_mac.h>
#include <esp_wifi.h>

namespace CameraWatchdog {

extern const char WIFI_SETUP_PAGE[] PROGMEM;

namespace {

constexpr char WIFI_NVS_NAMESPACE[] = "wifi";
constexpr size_t SCAN_MAX_NETWORKS = 24;
constexpr uint32_t SCAN_TIMEOUT_MS = 15000UL;
constexpr uint32_t SCAN_MAX_AGE_MS = 20000UL;
constexpr uint32_t TEST_SETTLE_MS = 1500UL;
constexpr uint32_t TEST_ABORT_GRACE_MS = 600UL;
constexpr uint32_t TEST_START_DELAY_MS = 400UL;
constexpr uint32_t PORTAL_ACTIVITY_HOLD_MS = 60000UL;
constexpr uint32_t AP_CLOSE_AFTER_SUCCESS_MS = 12000UL;
constexpr uint32_t AP_CLOSE_AFTER_CONNECT_MS = 20000UL;
constexpr uint32_t AP_MAX_CONNECTED_MS = 10UL * 60UL * 1000UL;

struct ScanEntry {
    char ssid[WIFI_SSID_MAX_LENGTH + 1];
    int8_t rssi;
    uint8_t channel;
    bool secure;
};

enum class TestState : uint8_t {
    Idle,
    Running,
    Success,
    Failed
};
enum class ScanState : uint8_t {
    Idle,
    Scanning,
    Done,
    Failed
};

DNSServer dnsServer;

WifiCredential storedCredential = {};
bool storedValid = false;
bool setupBegun = false;

char apSsid[40] = {};
bool apActive = false;
uint32_t apOpenedAtMs = 0;
uint32_t apCloseAtMs = 0;
uint32_t apNotBeforeMs = 0;

bool stationTracked = false;
bool stationConnected = false;
uint32_t stationChangedAtMs = 0;

bool holdActive = false;
bool autoReconnectEnabled = true;
uint32_t holdUntilMs = 0;
uint32_t lastManagementTickMs = 0;

ScanEntry scanResults[SCAN_MAX_NETWORKS];
size_t scanCount = 0;
ScanState scanState = ScanState::Idle;
uint32_t scanStartedAtMs = 0;
uint32_t scanFinishedAtMs = 0;

TestState testState = TestState::Idle;
char testSsid[WIFI_SSID_MAX_LENGTH + 1] = {};
char testPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
char testCode[24] = {};
uint32_t testStartedAtMs = 0;
bool testStationPending = false;
uint32_t testFinishedAtMs = 0;
uint32_t testConnectedAtMs = 0;
uint32_t testLastSampleAtMs = 0;
int32_t testRssiSum = 0;
uint16_t testRssiCount = 0;
int testRssi = 0;
uint8_t testChannel = 0;
bool testSaved = false;
IPAddress testIp;
IPAddress testGateway;

volatile uint8_t lastDisconnectReason = 0;
volatile uint32_t lastDisconnectAtMs = 0;
volatile int16_t scanDoneCount = WIFI_SCAN_FAILED;
volatile bool scanDoneFlag = false;

bool timeReached(uint32_t now, uint32_t target)
{
    return static_cast<int32_t>(now - target) >= 0;
}

bool hasAnyCredential()
{
    return storedValid || (strlen(WIFI_SSID) > 0 && strlen(WIFI_SSID) <= WIFI_SSID_MAX_LENGTH);
}

void loadStoredCredential()
{
    storedValid = false;
    memset(&storedCredential, 0, sizeof(storedCredential));
    Preferences storage;
    if (!storage.begin(WIFI_NVS_NAMESPACE, true))
        return;
    const String ssid = storage.getString("ssid", "");
    const String password = storage.getString("pass", "");
    storage.end();
    if (ssid.isEmpty() || ssid.length() > WIFI_SSID_MAX_LENGTH || password.length() > WIFI_PASSWORD_MAX_LENGTH)
        return;
    strlcpy(storedCredential.ssid, ssid.c_str(), sizeof(storedCredential.ssid));
    strlcpy(storedCredential.password, password.c_str(), sizeof(storedCredential.password));
    storedValid = true;
}

bool saveStoredCredential(const char* ssid, const char* password)
{
    Preferences storage;
    if (!storage.begin(WIFI_NVS_NAMESPACE, false))
        return false;
    const bool ssidSaved = storage.putString("ssid", ssid) == strlen(ssid);
    const bool passwordSaved = strlen(password) == 0 ? storage.remove("pass") || !storage.isKey("pass") : storage.putString("pass", password) == strlen(password);
    storage.end();
    loadStoredCredential();
    return ssidSaved && passwordSaved && storedValid && strcmp(storedCredential.ssid, ssid) == 0;
}

int signalPercent(int rssi)
{
    if (rssi >= -50)
        return 100;
    if (rssi <= -100)
        return 0;
    return 2 * (rssi + 100);
}

const char* signalQuality(int rssi)
{
    if (rssi >= -55)
        return "excellent";
    if (rssi >= -67)
        return "good";
    if (rssi >= -75)
        return "fair";
    if (rssi >= -85)
        return "weak";
    return "very_weak";
}

const char* disconnectCode(uint8_t reason)
{
    switch (reason) {
        case 201:
            return "network_not_found";
        case 202:
        case 204:
        case 14:
        case 15:
        case 16:
            return "wrong_password";
        case 203:
        case 5:
            return "association_refused";
        default:
            return "connect_failed";
    }
}

void touchPortal()
{
    holdUntilMs = millis() + PORTAL_ACTIVITY_HOLD_MS;
}

void syncAutoReconnect()
{
    const bool wanted = !holdActive && !apActive;
    if (wanted == autoReconnectEnabled)
        return;
    autoReconnectEnabled = wanted;
    WiFi.setAutoReconnect(wanted);
}

void setHold(bool on)
{
    if (on == holdActive)
        return;
    holdActive = on;
    if (on && WiFi.status() != WL_CONNECTED)
        WiFi.disconnect(false);
    syncAutoReconnect();
}

void closeAccessPoint(const char* reason)
{
    if (!apActive)
        return;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    apCloseAtMs = 0;
    syncAutoReconnect();
    Serial.printf("Setup network closed (%s).\n", reason);
    PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Info, "setup_ap_closed", IPAddress(), 0, 0, millis() - apOpenedAtMs, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Info, true);
}

void openAccessPoint()
{
    if (apActive)
        return;
    WiFi.mode(WIFI_AP_STA);
    const bool secured = strlen(WIFI_SETUP_AP_PASSWORD) >= 8;
    const bool started = WiFi.softAP(apSsid, secured ? WIFI_SETUP_AP_PASSWORD : nullptr, WIFI_SETUP_AP_CHANNEL, 0, 4);
    if (!started) {
        Serial.println("Failed to open the setup network.");
        apNotBeforeMs = millis() + 30000UL;
        WiFi.mode(WIFI_STA);
        return;
    }
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());
    apActive = true;
    apOpenedAtMs = millis();
    apCloseAtMs = 0;
    if (WiFi.status() != WL_CONNECTED)
        WiFi.disconnect(false);
    syncAutoReconnect();
    touchPortal();
    startHttpServer();
    Serial.printf("Setup network open: \"%s\" (%s). Open http://%s/\n", apSsid, secured ? "password protected" : "open", WiFi.softAPIP().toString().c_str());
    PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Started, "setup_ap_open", WiFi.softAPIP(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true);
}

void startScan()
{
    if (scanState == ScanState::Scanning || testState == TestState::Running)
        return;
    touchPortal();
    setHold(true);
    scanCount = 0;
    const int started = WiFi.scanNetworks(true, false, false, 300);
    scanStartedAtMs = millis();
    scanState = started == WIFI_SCAN_FAILED ? ScanState::Failed : ScanState::Scanning;
    if (scanState == ScanState::Failed)
        scanFinishedAtMs = scanStartedAtMs;
}

void collectScanResults(int found)
{
    scanCount = 0;
    for (int index = 0; index < found; index++) {
        const String ssid = WiFi.SSID(index);
        if (ssid.isEmpty() || ssid.length() > WIFI_SSID_MAX_LENGTH)
            continue;
        const int8_t rssi = static_cast<int8_t>(WiFi.RSSI(index));
        size_t slot = SCAN_MAX_NETWORKS;
        for (size_t existing = 0; existing < scanCount; existing++) {
            if (strcmp(scanResults[existing].ssid, ssid.c_str()) == 0) {
                slot = existing;
                break;
            }
        }
        if (slot == SCAN_MAX_NETWORKS) {
            if (scanCount >= SCAN_MAX_NETWORKS)
                continue;
            slot = scanCount++;
        } else if (scanResults[slot].rssi >= rssi) {
            continue;
        }
        strlcpy(scanResults[slot].ssid, ssid.c_str(), sizeof(scanResults[slot].ssid));
        scanResults[slot].rssi = rssi;
        scanResults[slot].channel = static_cast<uint8_t>(WiFi.channel(index));
        scanResults[slot].secure = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    }
    for (size_t outer = 1; outer < scanCount; outer++) {
        const ScanEntry current = scanResults[outer];
        size_t inner = outer;
        while (inner > 0 && scanResults[inner - 1].rssi < current.rssi) {
            scanResults[inner] = scanResults[inner - 1];
            inner--;
        }
        scanResults[inner] = current;
    }
}

void tickScan(uint32_t now)
{
    if (scanDoneFlag) {
        const int found = scanDoneCount;
        scanDoneFlag = false;
        if (scanState == ScanState::Scanning) {
            if (found >= 0) {
                collectScanResults(found);
                scanState = ScanState::Done;
            } else {
                scanState = ScanState::Failed;
            }
            scanFinishedAtMs = now;
        }
        WiFi.scanDelete();
        return;
    }
    if (scanState == ScanState::Scanning && now - scanStartedAtMs >= SCAN_TIMEOUT_MS) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        scanState = ScanState::Failed;
        scanFinishedAtMs = now;
    }
}

void finishTest(bool success, const char* code)
{
    testFinishedAtMs = millis();
    strlcpy(testCode, code, sizeof(testCode));
    if (success) {
        testState = TestState::Success;
        testRssi = testRssiCount > 0 ? static_cast<int>(testRssiSum / testRssiCount) : WiFi.RSSI();
        testChannel = static_cast<uint8_t>(WiFi.channel());
        testIp = WiFi.localIP();
        testGateway = WiFi.gatewayIP();
        testSaved = saveStoredCredential(testSsid, testPassword);
        if (!testSaved)
            strlcpy(testCode, "save_failed", sizeof(testCode));
        if (apActive)
            apCloseAtMs = testFinishedAtMs + AP_CLOSE_AFTER_SUCCESS_MS;
        Serial.printf("Wi-Fi configured: \"%s\" (%d dBm, IP %s)%s\n", testSsid, testRssi, testIp.toString().c_str(), testSaved ? "" : " - FAILED to save to NVS");
        PersistentLog::record(PersistentLog::Event::WifiConnect, testSaved ? PersistentLog::Outcome::Success : PersistentLog::Outcome::Failure, testSaved ? "setup_saved" : "setup_save_failed", testGateway, 0, testRssi, testFinishedAtMs - testStartedAtMs, 0, PersistentLog::FlagManual, testSaved ? PersistentLog::Level::Info : PersistentLog::Level::Error, true);
    } else {
        testState = TestState::Failed;
        testSaved = false;
        WiFi.disconnect(false);
        if (!apActive)
            holdUntilMs = 0;
        Serial.printf("Wi-Fi test failed (\"%s\"): %s\n", testSsid, testCode);
        PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Failure, testCode, IPAddress(), 0, lastDisconnectReason, testFinishedAtMs - testStartedAtMs, 0, PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
    }
    memset(testPassword, 0, sizeof(testPassword));
}

void tickTest(uint32_t now)
{
    if (testState != TestState::Running)
        return;
    if (testStationPending) {
        if (!timeReached(now, testStartedAtMs))
            return;
        testStationPending = false;
        WiFi.disconnect(false);
        delay(50);
        lastDisconnectReason = 0;
        WiFi.begin(testSsid, strlen(testPassword) > 0 ? testPassword : nullptr);
        Serial.printf("Testing Wi-Fi \"%s\"...\n", testSsid);
        PersistentLog::record(PersistentLog::Event::WifiConnect, PersistentLog::Outcome::Started, "setup_test", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagManual, PersistentLog::Level::Info, true);
        return;
    }
    const uint32_t elapsed = now - testStartedAtMs;
    const bool connected = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0) && WiFi.SSID() == testSsid;
    if (connected) {
        if (testConnectedAtMs == 0) {
            testConnectedAtMs = now;
            testRssiSum = 0;
            testRssiCount = 0;
            testLastSampleAtMs = 0;
        }
        if (now - testLastSampleAtMs >= 200) {
            testLastSampleAtMs = now;
            testRssiSum += WiFi.RSSI();
            testRssiCount++;
        }
        if (now - testConnectedAtMs >= TEST_SETTLE_MS)
            finishTest(true, "ok");
        return;
    }
    testConnectedAtMs = 0;
    const uint8_t reason = lastDisconnectReason;
    if (reason != 0 && reason != 8 && timeReached(lastDisconnectAtMs, testStartedAtMs + TEST_ABORT_GRACE_MS)) {
        finishTest(false, disconnectCode(reason));
        return;
    }
    if (elapsed >= WIFI_SETUP_TEST_TIMEOUT_MS)
        finishTest(false, WiFi.status() == WL_CONNECTED ? "dhcp_failed" : "timeout");
}

bool beginTest(const char* ssid, const char* password)
{
    if (testState == TestState::Running)
        return false;
    if (scanState == ScanState::Scanning) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        scanState = ScanState::Idle;
    }
    strlcpy(testSsid, ssid, sizeof(testSsid));
    strlcpy(testPassword, password, sizeof(testPassword));
    testCode[0] = '\0';
    testSaved = false;
    testRssi = 0;
    testChannel = 0;
    testIp = IPAddress();
    testGateway = IPAddress();
    testConnectedAtMs = 0;
    testRssiSum = 0;
    testRssiCount = 0;
    lastDisconnectReason = 0;
    testStartedAtMs = millis() + TEST_START_DELAY_MS;
    testStationPending = true;
    testFinishedAtMs = 0;
    testState = TestState::Running;
    touchPortal();
    setHold(true);
    return true;
}

bool clientIsOnAccessPoint()
{
    return apActive && httpServer.client().localIP() == WiFi.softAPIP();
}

void appendIp(String& json, const IPAddress& ip)
{
    json += '"';
    if (ip != IPAddress(0, 0, 0, 0))
        json += ip.toString();
    json += '"';
}

const char* scanStateName()
{
    switch (scanState) {
        case ScanState::Scanning:
            return "scanning";
        case ScanState::Done:
            return "done";
        case ScanState::Failed:
            return "failed";
        default:
            return "idle";
    }
}

void handleStatus()
{
    touchPortal();
    const bool connected = WiFi.status() == WL_CONNECTED;
    const uint32_t now = millis();
    String json;
    json.reserve(1100);
    json += "{\"ok\":true,\"firmware\":\"";
    json += FIRMWARE_VERSION;
    json += "\",\"hostname\":\"";
    appendJsonEscaped(json, String(globalSettings.localHostname));
    json += "\",\"connected\":";
    json += connected ? "true" : "false";
    json += ",\"ssid\":\"";
    if (connected)
        appendJsonEscaped(json, WiFi.SSID());
    json += "\",\"rssi\":";
    json += connected ? WiFi.RSSI() : 0;
    json += ",\"channel\":";
    json += connected ? WiFi.channel() : 0;
    json += ",\"ip\":";
    appendIp(json, connected ? WiFi.localIP() : IPAddress());
    json += ",\"viaAccessPoint\":";
    json += clientIsOnAccessPoint() ? "true" : "false";
    json += ",\"accessPoint\":{\"active\":";
    json += apActive ? "true" : "false";
    json += ",\"ssid\":\"";
    appendJsonEscaped(json, String(apSsid));
    json += "\",\"ip\":";
    appendIp(json, apActive ? WiFi.softAPIP() : IPAddress());
    json += ",\"stations\":";
    json += apActive ? WiFi.softAPgetStationNum() : 0;
    json += ",\"secured\":";
    json += strlen(WIFI_SETUP_AP_PASSWORD) >= 8 ? "true" : "false";
    json += "},\"saved\":{\"stored\":";
    json += storedValid ? "true" : "false";
    json += ",\"ssid\":\"";
    if (storedValid)
        appendJsonEscaped(json, String(storedCredential.ssid));
    json += "\",\"builtin\":";
    json += strlen(WIFI_SSID) > 0 ? "true" : "false";
    json += "},\"scan\":{\"state\":\"";
    json += scanStateName();
    json += "\",\"count\":";
    json += scanCount;
    json += "},\"test\":{\"state\":\"";
    switch (testState) {
        case TestState::Running:
            json += "running";
            break;
        case TestState::Success:
            json += "success";
            break;
        case TestState::Failed:
            json += "failed";
            break;
        default:
            json += "idle";
            break;
    }
    json += "\",\"ssid\":\"";
    appendJsonEscaped(json, String(testSsid));
    json += "\",\"phase\":\"";
    json += testConnectedAtMs != 0 ? "measuring" : "connecting";
    json += "\",\"code\":\"";
    json += testCode;
    json += "\",\"rssi\":";
    json += testState == TestState::Success ? testRssi : 0;
    json += ",\"percent\":";
    json += testState == TestState::Success ? signalPercent(testRssi) : 0;
    json += ",\"quality\":\"";
    json += testState == TestState::Success ? signalQuality(testRssi) : "";
    json += "\",\"channel\":";
    json += testState == TestState::Success ? testChannel : 0;
    json += ",\"ip\":";
    appendIp(json, testState == TestState::Success ? testIp : IPAddress());
    json += ",\"gateway\":";
    appendIp(json, testState == TestState::Success ? testGateway : IPAddress());
    json += ",\"saved\":";
    json += testSaved ? "true" : "false";
    json += ",\"elapsedMs\":";
    json += testState == TestState::Running ? (testStationPending ? 0 : now - testStartedAtMs) : (testFinishedAtMs - testStartedAtMs);
    json += "},\"security\":{\"passwordRequired\":";
    json += adminPasswordConfigured() ? "true" : "false";
    json += ",\"writeToken\":\"";
    json += settingsWriteToken;
    json += "\"}}";
    sendJsonResponse(200, json);
}

void handleScan()
{
    touchPortal();
    const uint32_t now = millis();
    const bool refresh = httpServer.arg("refresh") == "1";
    const bool stale = scanState == ScanState::Idle || scanState == ScanState::Failed || now - scanFinishedAtMs > SCAN_MAX_AGE_MS;
    if (scanState != ScanState::Scanning && testState != TestState::Running && (refresh || stale))
        startScan();

    const bool connected = WiFi.status() == WL_CONNECTED;
    const String currentSsid = connected ? WiFi.SSID() : String();
    String json;
    json.reserve(256 + scanCount * 96);
    json += "{\"ok\":true,\"state\":\"";
    json += testState == TestState::Running ? "busy" : scanStateName();
    json += "\",\"networks\":[";
    for (size_t index = 0; index < scanCount; index++) {
        if (index > 0)
            json += ',';
        json += "{\"ssid\":\"";
        appendJsonEscaped(json, String(scanResults[index].ssid));
        json += "\",\"rssi\":";
        json += scanResults[index].rssi;
        json += ",\"percent\":";
        json += signalPercent(scanResults[index].rssi);
        json += ",\"quality\":\"";
        json += signalQuality(scanResults[index].rssi);
        json += "\",\"channel\":";
        json += scanResults[index].channel;
        json += ",\"secure\":";
        json += scanResults[index].secure ? "true" : "false";
        json += ",\"saved\":";
        json += storedValid && strcmp(storedCredential.ssid, scanResults[index].ssid) == 0 ? "true" : "false";
        json += ",\"current\":";
        json += connected && currentSsid == scanResults[index].ssid ? "true" : "false";
        json += '}';
    }
    json += "]}";
    sendJsonResponse(200, json);
}

bool validText(const String& text)
{
    for (size_t index = 0; index < text.length(); index++) {
        const uint8_t value = static_cast<uint8_t>(text.charAt(index));
        if (value < 0x20 || value == 0x7F)
            return false;
    }
    return true;
}

void handleConnect()
{
    touchPortal();
    String authorizationError;
    if (!authorizeDashboardMutation(authorizationError, false)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }
    const String ssid = httpServer.arg("ssid");
    const String password = httpServer.arg("password");
    if (ssid.isEmpty() || ssid.length() > WIFI_SSID_MAX_LENGTH || !validText(ssid)) {
        sendApiError(400, "invalid_ssid", "The network name must be 1 to 32 characters long.");
        return;
    }
    if (password.length() > WIFI_PASSWORD_MAX_LENGTH || (!password.isEmpty() && password.length() < 8) || !validText(password)) {
        sendApiError(400, "invalid_password", "The password must be 8 to 63 characters long.");
        return;
    }
    if (factoryResetRequestedAtMs != 0) {
        sendApiError(409, "busy", "Factory reset in progress.");
        return;
    }
    if (!beginTest(ssid.c_str(), password.c_str())) {
        sendApiError(409, "busy", "A test is already in progress.");
        return;
    }
    sendJsonResponse(202, String("{\"ok\":true}"));
}

void handleForget()
{
    touchPortal();
    String authorizationError;
    if (!authorizeDashboardMutation(authorizationError, false)) {
        sendApiError(403, "forbidden", authorizationError);
        return;
    }
    wifiSetupForgetStored();
    sendJsonResponse(200, String("{\"ok\":true}"));
}

void handlePage()
{
    touchPortal();
    httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    httpServer.sendHeader("Pragma", "no-cache");
    httpServer.send_P(200, "text/html; charset=utf-8", WIFI_SETUP_PAGE);
}

void handleNotFound()
{
    if (wifiSetupHandleNotFound())
        return;
    httpServer.send(404, "application/json", "{\"error\":\"not_found\"}");
}

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        lastDisconnectReason = info.wifi_sta_disconnected.reason;
        lastDisconnectAtMs = millis();
    } else if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {
        scanDoneCount = static_cast<int16_t>(info.wifi_scan_done.status == 0 ? info.wifi_scan_done.number : WIFI_SCAN_FAILED);
        scanDoneFlag = true;
    }
}

}

void wifiSetupBegin()
{
    if (setupBegun)
        return;
    setupBegun = true;

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", WIFI_SETUP_AP_PREFIX, mac[4], mac[5]);

    loadStoredCredential();
    WiFi.onEvent(onWifiEvent);

    httpServer.on("/wifi", HTTP_GET, handlePage);
    httpServer.on("/api/wifi/status", HTTP_GET, handleStatus);
    httpServer.on("/api/wifi/scan", HTTP_GET, handleScan);
    httpServer.on("/api/wifi/connect", HTTP_POST, handleConnect);
    httpServer.on("/api/wifi/forget", HTTP_POST, handleForget);
    httpServer.onNotFound(handleNotFound);

    stationTracked = true;
    stationConnected = false;
    stationChangedAtMs = millis();
    Serial.printf("Wi-Fi: %s\n", storedValid ? "credentials saved from the dashboard found" : (strlen(WIFI_SSID) > 0 ? "using firmware credentials" : "no credentials configured"));
}

size_t wifiCredentialCandidates(WifiCredential* candidates)
{
    size_t count = 0;
    if (storedValid)
        candidates[count++] = storedCredential;
    const size_t builtinSsidLength = strlen(WIFI_SSID);
    if (builtinSsidLength > 0 && builtinSsidLength <= WIFI_SSID_MAX_LENGTH && strlen(WIFI_PASSWORD) <= WIFI_PASSWORD_MAX_LENGTH) {
        WifiCredential builtin = {};
        strlcpy(builtin.ssid, WIFI_SSID, sizeof(builtin.ssid));
        strlcpy(builtin.password, WIFI_PASSWORD, sizeof(builtin.password));
        if (count == 0 || strcmp(candidates[0].ssid, builtin.ssid) != 0 || strcmp(candidates[0].password, builtin.password) != 0)
            candidates[count++] = builtin;
    }
    return count;
}

void wifiSetupForgetStored()
{
    Preferences storage;
    if (storage.begin(WIFI_NVS_NAMESPACE, false)) {
        storage.clear();
        storage.end();
    }
    loadStoredCredential();
    if (testState == TestState::Success) {
        testState = TestState::Idle;
        testSaved = false;
    }
    PersistentLog::record(PersistentLog::Event::SettingsReset, PersistentLog::Outcome::Success, "wifi_forgotten", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagManual, PersistentLog::Level::Info, true);
}

bool wifiSetupHoldsStation()
{
    return holdActive;
}

bool wifiSetupAccessPointActive()
{
    return apActive;
}

bool wifiSetupHandleNotFound()
{
    if (!clientIsOnAccessPoint())
        return false;
    if (httpServer.uri().startsWith("/api/"))
        return false;
    String location = "http://";
    location += WiFi.softAPIP().toString();
    location += "/wifi";
    httpServer.sendHeader("Location", location, true);
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.send(302, "text/plain", "");
    return true;
}

void wifiSetupLoop()
{
    if (!setupBegun)
        return;
    const uint32_t now = millis();

    if (apActive)
        dnsServer.processNextRequest();
    tickScan(now);
    tickTest(now);

    if (now - lastManagementTickMs < 250)
        return;
    lastManagementTickMs = now;

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (!stationTracked || connected != stationConnected) {
        stationTracked = true;
        stationConnected = connected;
        stationChangedAtMs = now;
    }
    const uint8_t stations = apActive ? WiFi.softAPgetStationNum() : 0;

    const bool wantHold = testState == TestState::Running || (!connected && (!timeReached(now, holdUntilMs) || stations > 0));
    setHold(wantHold);

    if (testState == TestState::Running)
        return;

    if (!apActive) {
        if (!connected && timeReached(now, apNotBeforeMs)) {
            if (!hasAnyCredential() || now - stationChangedAtMs >= WIFI_SETUP_AP_DELAY_MS)
                openAccessPoint();
        }
        return;
    }

    if (apCloseAtMs != 0 && timeReached(now, apCloseAtMs)) {
        closeAccessPoint("setup complete");
    } else if (connected) {
        const uint32_t connectedFor = now - stationChangedAtMs;
        if (connectedFor >= AP_MAX_CONNECTED_MS || (connectedFor >= AP_CLOSE_AFTER_CONNECT_MS && stations == 0 && timeReached(now, holdUntilMs)))
            closeAccessPoint("Wi-Fi connected");
    } else if (hasAnyCredential() && now - apOpenedAtMs >= WIFI_SETUP_AP_WINDOW_MS && stations == 0 && timeReached(now, holdUntilMs)) {
        apNotBeforeMs = now + WIFI_SETUP_AP_REOPEN_MS;
        closeAccessPoint("timed out");
    }
}

}
