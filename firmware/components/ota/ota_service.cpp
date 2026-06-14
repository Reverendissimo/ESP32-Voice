/**
 * @file ota_service.cpp
 * @brief Implementation of OtaService.
 */
#include "ota_service.hpp"

#include <stdio.h>
#include <string.h>

#include "audio_playback_service.hpp"
#include "config_callbacks.hpp"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utterance_state_machine.hpp"

#include "cJSON.h"

static const char* kTag = "ota_service";
static constexpr size_t kHttpBufferBytes = 4096;
static constexpr int kHttpTimeoutMs = 30000;

namespace {

void copyString(char* dest, size_t destSize, const char* value) {
    if (dest == nullptr || destSize == 0) {
        return;
    }
    if (value == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, destSize - 1);
    dest[destSize - 1] = '\0';
}

bool versionsDiffer(const char* current, const char* offered) {
    if (current == nullptr || offered == nullptr || offered[0] == '\0') {
        return false;
    }
    return strcmp(current, offered) != 0;
}

struct UpdateTaskArgs {
    OtaService* service;
    char url[256];
    bool force;
};

}  // namespace

void OtaService::configure(
    const config::OtaConfig& ota,
    const char* callbackBaseUrl,
    AudioPlaybackService* playback,
    UtteranceStateMachine* utteranceFsm) {
    m_ota = ota;
    copyString(m_callbackBaseUrl, sizeof(m_callbackBaseUrl), callbackBaseUrl);
    m_playback = playback;
    m_utteranceFsm = utteranceFsm;
}

void OtaService::refreshConfig(const config::OtaConfig& ota, const char* callbackBaseUrl) {
    m_ota = ota;
    copyString(m_callbackBaseUrl, sizeof(m_callbackBaseUrl), callbackBaseUrl);
}

bool OtaService::isEnabled() const {
    return m_ota.secret[0] != '\0';
}

bool OtaService::authorize(const char* providedSecret) const {
    if (!isEnabled()) {
        return false;
    }
    if (providedSecret == nullptr || providedSecret[0] == '\0') {
        return false;
    }
    return strcmp(m_ota.secret, providedSecret) == 0;
}

OtaState OtaService::state() const {
    return m_state;
}

uint8_t OtaService::progressPercent() const {
    return m_progressPercent;
}

const char* OtaService::lastError() const {
    return m_lastError;
}

const char* OtaService::targetVersion() const {
    return m_targetVersion;
}

bool OtaService::isSystemBusy() const {
    if (m_playback != nullptr && m_playback->isBusy()) {
        return true;
    }
    if (m_utteranceFsm != nullptr && m_utteranceFsm->isStreaming()) {
        return true;
    }
    return false;
}

void OtaService::validateRunningImageOnBoot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return;
    }

    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) != ESP_OK) {
        return;
    }

    if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(kTag, "First boot after OTA on %s — marking image valid", running->label);
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to mark OTA image valid: %s", esp_err_to_name(err));
        }
    }
}

bool OtaService::resolveManifest(
    char* firmwareUrl,
    size_t firmwareUrlLen,
    char* versionOut,
    size_t versionOutLen) {
    if (firmwareUrl == nullptr || firmwareUrlLen == 0) {
        return false;
    }
    firmwareUrl[0] = '\0';
    if (versionOut != nullptr && versionOutLen > 0) {
        versionOut[0] = '\0';
    }

    config::AppConfig config = {};
    config.ota = m_ota;
    copyString(config.network.callbackBaseUrl, sizeof(config.network.callbackBaseUrl), m_callbackBaseUrl);

    char manifestUrl[192] = {};
    config::resolveOtaManifestUrl(config, manifestUrl, sizeof(manifestUrl));
    if (manifestUrl[0] == '\0') {
        copyString(m_lastError, sizeof(m_lastError), "OTA manifest URL not configured");
        return false;
    }

    esp_http_client_config_t httpConfig = {};
    httpConfig.url = manifestUrl;
    httpConfig.timeout_ms = kHttpTimeoutMs;
    httpConfig.buffer_size = kHttpBufferBytes;
    httpConfig.buffer_size_tx = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
    if (client == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "Manifest HTTP init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        snprintf(m_lastError, sizeof(m_lastError), "Manifest open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int contentLength = esp_http_client_fetch_headers(client);
    if (contentLength <= 0 || contentLength > 4096) {
        copyString(m_lastError, sizeof(m_lastError), "Manifest response invalid");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char* body = static_cast<char*>(malloc(static_cast<size_t>(contentLength) + 1));
    if (body == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "Manifest alloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int totalRead = 0;
    while (totalRead < contentLength) {
        const int read = esp_http_client_read(client, body + totalRead, contentLength - totalRead);
        if (read <= 0) {
            break;
        }
        totalRead += read;
    }
    body[totalRead] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON* root = cJSON_Parse(body);
    free(body);
    if (root == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "Manifest JSON parse failed");
        return false;
    }

    const cJSON* versionNode = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* urlNode = cJSON_GetObjectItemCaseSensitive(root, "url");
    if (!cJSON_IsString(versionNode) || !cJSON_IsString(urlNode) || urlNode->valuestring == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "Manifest missing version/url");
        cJSON_Delete(root);
        return false;
    }

    copyString(firmwareUrl, firmwareUrlLen, urlNode->valuestring);
    if (versionOut != nullptr && versionOutLen > 0) {
        copyString(versionOut, versionOutLen, versionNode->valuestring);
    }
    cJSON_Delete(root);
    return firmwareUrl[0] != '\0';
}

bool OtaService::downloadAndInstall(const char* firmwareUrl) {
    if (firmwareUrl == nullptr || firmwareUrl[0] == '\0') {
        copyString(m_lastError, sizeof(m_lastError), "Missing firmware URL");
        return false;
    }

    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (updatePartition == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "No OTA update partition");
        return false;
    }

    ESP_LOGI(
        kTag,
        "OTA download start -> %s (target partition %s)",
        firmwareUrl,
        updatePartition->label);

    esp_http_client_config_t httpConfig = {};
    httpConfig.url = firmwareUrl;
    httpConfig.timeout_ms = kHttpTimeoutMs;
    httpConfig.buffer_size = kHttpBufferBytes;
    httpConfig.buffer_size_tx = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
    if (client == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "Firmware HTTP init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        snprintf(m_lastError, sizeof(m_lastError), "Firmware open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    const int contentLength = esp_http_client_fetch_headers(client);
    if (contentLength <= 0) {
        copyString(m_lastError, sizeof(m_lastError), "Firmware content length unknown");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_ota_handle_t updateHandle = 0;
    err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &updateHandle);
    if (err != ESP_OK) {
        snprintf(m_lastError, sizeof(m_lastError), "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    auto* buffer = static_cast<uint8_t*>(malloc(kHttpBufferBytes));
    if (buffer == nullptr) {
        esp_ota_abort(updateHandle);
        copyString(m_lastError, sizeof(m_lastError), "Download buffer alloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int totalWritten = 0;
    bool ok = true;
    while (true) {
        const int read = esp_http_client_read(client, reinterpret_cast<char*>(buffer), kHttpBufferBytes);
        if (read < 0) {
            snprintf(m_lastError, sizeof(m_lastError), "Firmware read failed");
            ok = false;
            break;
        }
        if (read == 0) {
            break;
        }

        err = esp_ota_write(updateHandle, buffer, static_cast<size_t>(read));
        if (err != ESP_OK) {
            snprintf(m_lastError, sizeof(m_lastError), "esp_ota_write failed: %s", esp_err_to_name(err));
            ok = false;
            break;
        }

        totalWritten += read;
        m_progressPercent = static_cast<uint8_t>((totalWritten * 100) / contentLength);
        if (m_progressPercent > 100) {
            m_progressPercent = 100;
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok) {
        esp_ota_abort(updateHandle);
        return false;
    }

    err = esp_ota_end(updateHandle);
    if (err != ESP_OK) {
        snprintf(m_lastError, sizeof(m_lastError), "esp_ota_end failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        snprintf(m_lastError, sizeof(m_lastError), "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return false;
    }

    m_progressPercent = 100;
    m_state = OtaState::Rebooting;
    ESP_LOGI(kTag, "OTA complete (%d bytes) — rebooting into %s", totalWritten, updatePartition->label);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return true;
}

void OtaService::runUpdate(const char* firmwareUrl, bool force) {
    m_lastError[0] = '\0';
    m_targetVersion[0] = '\0';
    m_progressPercent = 0;
    m_state = OtaState::Downloading;

    if (isSystemBusy()) {
        m_state = OtaState::Failed;
        copyString(m_lastError, sizeof(m_lastError), "Device busy (audio active)");
        return;
    }

    char resolvedUrl[256] = {};
    char offeredVersion[32] = {};
    if (firmwareUrl != nullptr && firmwareUrl[0] != '\0') {
        copyString(resolvedUrl, sizeof(resolvedUrl), firmwareUrl);
    } else if (!resolveManifest(resolvedUrl, sizeof(resolvedUrl), offeredVersion, sizeof(offeredVersion))) {
        m_state = OtaState::Failed;
        return;
    } else {
        copyString(m_targetVersion, sizeof(m_targetVersion), offeredVersion);
    }

    const esp_app_desc_t* running = esp_app_get_description();
    const char* currentVersion = (running != nullptr) ? running->version : "";
    if (offeredVersion[0] != '\0') {
        copyString(m_targetVersion, sizeof(m_targetVersion), offeredVersion);
    }

    if (!force && offeredVersion[0] != '\0' && !versionsDiffer(currentVersion, offeredVersion)) {
        m_state = OtaState::Idle;
        copyString(m_lastError, sizeof(m_lastError), "Already running target version");
        ESP_LOGI(kTag, "OTA skipped — already on version %s", currentVersion);
        return;
    }

    if (!downloadAndInstall(resolvedUrl)) {
        m_state = OtaState::Failed;
    }
}

void OtaService::updateTask(void* arg) {
    auto* args = static_cast<UpdateTaskArgs*>(arg);
    if (args == nullptr || args->service == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    args->service->runUpdate(args->url[0] != '\0' ? args->url : nullptr, args->force);
    args->service->m_taskHandle = nullptr;
    free(args);
    vTaskDelete(nullptr);
}

bool OtaService::startUpdate(const char* firmwareUrl, bool force) {
    if (!isEnabled()) {
        copyString(m_lastError, sizeof(m_lastError), "OTA not configured");
        return false;
    }
    if (m_state == OtaState::Downloading || m_state == OtaState::Rebooting) {
        copyString(m_lastError, sizeof(m_lastError), "OTA already in progress");
        return false;
    }
    if (m_taskHandle != nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "OTA task already running");
        return false;
    }

    auto* args = static_cast<UpdateTaskArgs*>(calloc(1, sizeof(UpdateTaskArgs)));
    if (args == nullptr) {
        copyString(m_lastError, sizeof(m_lastError), "OTA task alloc failed");
        return false;
    }

    args->service = this;
    args->force = force;
    if (firmwareUrl != nullptr) {
        copyString(args->url, sizeof(args->url), firmwareUrl);
    }

    m_state = OtaState::Downloading;
    m_progressPercent = 0;
    m_lastError[0] = '\0';

    const BaseType_t created = xTaskCreate(
        updateTask,
        "ota_update",
        8192,
        args,
        5,
        reinterpret_cast<TaskHandle_t*>(&m_taskHandle));
    if (created != pdPASS) {
        free(args);
        m_state = OtaState::Failed;
        copyString(m_lastError, sizeof(m_lastError), "OTA task create failed");
        m_taskHandle = nullptr;
        return false;
    }
    return true;
}
