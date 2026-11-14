#include "firmware_internal.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <strings.h>

#include "live_stream.h"

extern "C" bool verifyRollbackLater()
{
    return true;
}

namespace CameraWatchdog {

namespace {

constexpr unsigned long MAINTENANCE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr unsigned long LIVE_RELAY_SUSPEND_WAIT_MS = 6000UL;
constexpr unsigned long ROLLBACK_VALIDATION_DELAY_MS = 20000UL;
constexpr size_t SHA256_BYTES = 32;
constexpr size_t SHA256_HEX_LENGTH = SHA256_BYTES * 2;

bool maintenanceActive = false;
unsigned long maintenanceDeadlineMs = 0;
String uploadToken;
char expectedSha256[SHA256_HEX_LENGTH + 1] = {};
mbedtls_sha256_context hashContext;
bool hashActive = false;
bool updateStarted = false;
bool uploadFailed = false;
bool uploadFinished = false;
String uploadError;
size_t uploadBytes = 0;
bool rollbackValidated = false;

void releaseUpload()
{
    if (hashActive) {
        mbedtls_sha256_free(&hashContext);
        hashActive = false;
    }

    if (updateStarted) {
        Update.abort();
        updateStarted = false;
    }
}

void failUpload(const String& message, const char* logDetail)
{
    releaseUpload();
    uploadFailed = true;
    uploadError = message;
    PersistentLog::record(PersistentLog::Event::AdminAction, PersistentLog::Outcome::Failure, logDetail, IPAddress(), 0, 0, 0, static_cast<uint32_t>(uploadBytes), PersistentLog::FlagManual, PersistentLog::Level::Error, true);
}

}

bool firmwareUpdateMaintenanceActive()
{
    return maintenanceActive;
}

bool firmwareUpdateValidSha256(const String& value)
{
    if (value.length() != SHA256_HEX_LENGTH) {
        return false;
    }

    for (size_t index = 0; index < value.length(); index++) {
        if (!isxdigit(static_cast<unsigned char>(value.charAt(index)))) {
            return false;
        }
    }

    return true;
}

bool firmwareUpdateConfirmed()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    return running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY;
}

bool firmwareUpdateEnterMaintenance(const String& sha256, String& token)
{
    releaseUpload();
    uploadFailed = false;
    uploadFinished = false;
    uploadError = String();
    uploadBytes = 0;
    strlcpy(expectedSha256, sha256.c_str(), sizeof(expectedSha256));

    liveStreamSetSuspended(true);
    const unsigned long waitStartedAtMs = millis();
    while (!liveStreamSuspended() && millis() - waitStartedAtMs < LIVE_RELAY_SUSPEND_WAIT_MS) {
        delay(10);
    }

    if (!liveStreamSuspended()) {
        if (!maintenanceActive) {
            liveStreamSetSuspended(false);
        }
        return false;
    }

    for (CameraContext* ctx = cameras.get(); ctx != nullptr; ctx = ctx->next.get()) {
        resetCameraSnapshotHttpSession(*ctx);
    }

    if (!maintenanceActive) {
        PersistentLog::record(PersistentLog::Event::AdminAction, PersistentLog::Outcome::Started, "firmware_update", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
    }

    uploadToken = makeSettingsWriteToken();
    token = uploadToken;
    maintenanceActive = true;
    maintenanceDeadlineMs = millis() + MAINTENANCE_TIMEOUT_MS;
    return true;
}

void firmwareUpdateLeaveMaintenance()
{
    releaseUpload();
    uploadToken = String();
    maintenanceActive = false;
    liveStreamSetSuspended(false);
}

bool firmwareUploadStart(const String& token)
{
    uploadFailed = false;
    uploadFinished = false;
    uploadError = String();
    uploadBytes = 0;

    if (!maintenanceActive || uploadToken.isEmpty() || !secureTextEquals(token, uploadToken.c_str())) {
        uploadFailed = true;
        uploadError = "The update was not prepared or expired; start it again";
        return false;
    }

    uploadToken = String();

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        failUpload(String("Could not start the update: ") + Update.errorString(), "update_begin_failed");
        return true;
    }

    updateStarted = true;
    mbedtls_sha256_init(&hashContext);
    mbedtls_sha256_starts(&hashContext, 0);
    hashActive = true;
    return true;
}

void firmwareUploadWrite(const uint8_t* data, size_t length)
{
    if (!updateStarted || uploadFailed) {
        return;
    }

    if (Update.write(const_cast<uint8_t*>(data), length) != length) {
        failUpload(String("Flash write failed: ") + Update.errorString(), "update_write_failed");
        return;
    }

    mbedtls_sha256_update(&hashContext, data, length);
    uploadBytes += length;
}

void firmwareUploadEnd()
{
    if (!updateStarted || uploadFailed) {
        return;
    }

    uint8_t digest[SHA256_BYTES];
    mbedtls_sha256_finish(&hashContext, digest);
    char digestHex[SHA256_HEX_LENGTH + 1];

    for (size_t index = 0; index < SHA256_BYTES; index++) {
        snprintf(digestHex + index * 2, 3, "%02x", digest[index]);
    }

    if (strcasecmp(digestHex, expectedSha256) != 0) {
        failUpload("The firmware does not match the published checksum", "update_hash_mismatch");
        return;
    }

    mbedtls_sha256_free(&hashContext);
    hashActive = false;

    if (!Update.end(true)) {
        updateStarted = false;
        failUpload(String("Could not finish the update: ") + Update.errorString(), "update_end_failed");
        return;
    }

    updateStarted = false;
    uploadFinished = true;
    espRestartRequestedAtMs = millis();
    PersistentLog::record(PersistentLog::Event::AdminAction, PersistentLog::Outcome::Success, "firmware_update", IPAddress(), 0, 0, 0, static_cast<uint32_t>(uploadBytes), PersistentLog::FlagManual, PersistentLog::Level::Warning, true);
}

void firmwareUploadAbort()
{
    if (updateStarted && !uploadFailed) {
        failUpload("The upload was interrupted", "update_aborted");
    }
}

bool firmwareUploadResult(String& error)
{
    if (uploadFinished) {
        return true;
    }

    error = uploadError.isEmpty() ? String("The upload did not include a firmware file") : uploadError;
    return false;
}

void firmwareUpdateMaintain()
{
    static const unsigned long loopStartedAtMs = millis();

    if (maintenanceActive && !uploadFinished && static_cast<long>(millis() - maintenanceDeadlineMs) >= 0) {
        firmwareUpdateLeaveMaintenance();
        PersistentLog::record(PersistentLog::Event::AdminAction, PersistentLog::Outcome::Failure, "update_timeout", IPAddress(), 0, 0, 0, 0, PersistentLog::FlagScheduled, PersistentLog::Level::Warning, true);
    }

    if (rollbackValidated || millis() - loopStartedAtMs < ROLLBACK_VALIDATION_DELAY_MS) {
        return;
    }

    if (!firmwareUpdateConfirmed()) {
        esp_ota_mark_app_valid_cancel_rollback();
    }

    rollbackValidated = firmwareUpdateConfirmed();
}

}
