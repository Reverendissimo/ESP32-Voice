/**
 * @file config_store.cpp
 * @brief Implementation of ConfigStore.
 */
#include "config_store.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* kTag = "config_store";
static const char* kNamespace = "config";
static const char* kSavedKey = "saved_json";

namespace {

void copyString(char* dest, size_t destSize, const char* value) {
    if (value == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, destSize - 1);
    dest[destSize - 1] = '\0';
}

void writeIdentity(const config::IdentityConfig& identity, cJSON* root) {
    cJSON_AddStringToObject(root, "deviceName", identity.deviceName);
}

void writeAuth(const config::AuthConfig& auth, cJSON* root) {
    cJSON_AddStringToObject(root, "token", auth.token);
}

void writeWifi(const config::WifiConfig& wifi, cJSON* root) {
    cJSON_AddStringToObject(root, "ssid", wifi.ssid);
    cJSON_AddStringToObject(root, "password", wifi.password);
}

void writeNetwork(const config::NetworkConfig& network, cJSON* root) {
    cJSON_AddStringToObject(root, "callbackBaseUrl", network.callbackBaseUrl);
    cJSON_AddNumberToObject(root, "localHttpPort", network.localHttpPort);
}

void writeCallbacks(const config::CallbacksConfig& callbacks, cJSON* root) {
    cJSON_AddStringToObject(root, "speechUrl", callbacks.speechUrl);
    cJSON_AddStringToObject(root, "speechFinalizeUrl", callbacks.speechFinalizeUrl);
    cJSON_AddStringToObject(root, "uiEventUrl", callbacks.uiEventUrl);
    cJSON_AddStringToObject(root, "heartbeatUrl", callbacks.heartbeatUrl);
}

void writeTime(const config::TimeConfig& time, cJSON* root) {
    cJSON_AddStringToObject(root, "timezone", time.timezone);
    cJSON_AddStringToObject(root, "sntpServer", time.sntpServer);
    cJSON_AddNumberToObject(root, "syncIntervalSec", time.syncIntervalSec);
}

bool readIdentity(const cJSON* root, config::IdentityConfig& identity) {
    const cJSON* deviceName = cJSON_GetObjectItemCaseSensitive(root, "deviceName");
    if (cJSON_IsString(deviceName)) {
        copyString(identity.deviceName, sizeof(identity.deviceName), deviceName->valuestring);
    }
    return true;
}

bool readAuth(const cJSON* root, config::AuthConfig& auth) {
    const cJSON* token = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (cJSON_IsString(token)) {
        copyString(auth.token, sizeof(auth.token), token->valuestring);
    }
    return true;
}

bool readWifi(const cJSON* root, config::WifiConfig& wifi) {
    const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON* password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (cJSON_IsString(ssid)) {
        copyString(wifi.ssid, sizeof(wifi.ssid), ssid->valuestring);
    }
    if (cJSON_IsString(password)) {
        copyString(wifi.password, sizeof(wifi.password), password->valuestring);
    }
    return true;
}

bool readCallbacks(const cJSON* root, config::CallbacksConfig& callbacks) {
    const cJSON* speechUrl = cJSON_GetObjectItemCaseSensitive(root, "speechUrl");
    const cJSON* speechFinalizeUrl = cJSON_GetObjectItemCaseSensitive(root, "speechFinalizeUrl");
    const cJSON* uiEventUrl = cJSON_GetObjectItemCaseSensitive(root, "uiEventUrl");
    const cJSON* heartbeatUrl = cJSON_GetObjectItemCaseSensitive(root, "heartbeatUrl");
    if (cJSON_IsString(speechUrl)) {
        copyString(callbacks.speechUrl, sizeof(callbacks.speechUrl), speechUrl->valuestring);
    }
    if (cJSON_IsString(speechFinalizeUrl)) {
        copyString(callbacks.speechFinalizeUrl, sizeof(callbacks.speechFinalizeUrl), speechFinalizeUrl->valuestring);
    }
    if (cJSON_IsString(uiEventUrl)) {
        copyString(callbacks.uiEventUrl, sizeof(callbacks.uiEventUrl), uiEventUrl->valuestring);
    }
    if (cJSON_IsString(heartbeatUrl)) {
        copyString(callbacks.heartbeatUrl, sizeof(callbacks.heartbeatUrl), heartbeatUrl->valuestring);
    }
    return true;
}

bool readNetwork(const cJSON* root, config::NetworkConfig& network) {
    const cJSON* callbackBaseUrl = cJSON_GetObjectItemCaseSensitive(root, "callbackBaseUrl");
    const cJSON* localHttpPort = cJSON_GetObjectItemCaseSensitive(root, "localHttpPort");
    if (cJSON_IsString(callbackBaseUrl)) {
        copyString(network.callbackBaseUrl, sizeof(network.callbackBaseUrl), callbackBaseUrl->valuestring);
    }
    if (cJSON_IsNumber(localHttpPort)) {
        network.localHttpPort = static_cast<uint16_t>(localHttpPort->valuedouble);
    }
    return true;
}

bool readTime(const cJSON* root, config::TimeConfig& time) {
    const cJSON* timezone = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    const cJSON* sntpServer = cJSON_GetObjectItemCaseSensitive(root, "sntpServer");
    const cJSON* syncIntervalSec = cJSON_GetObjectItemCaseSensitive(root, "syncIntervalSec");
    if (cJSON_IsString(timezone)) {
        copyString(time.timezone, sizeof(time.timezone), timezone->valuestring);
    }
    if (cJSON_IsString(sntpServer)) {
        copyString(time.sntpServer, sizeof(time.sntpServer), sntpServer->valuestring);
    }
    if (cJSON_IsNumber(syncIntervalSec)) {
        time.syncIntervalSec = static_cast<uint32_t>(syncIntervalSec->valuedouble);
    }
    return true;
}

cJSON* configToJson(const config::AppConfig& config) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return nullptr;
    }

    cJSON_AddNumberToObject(root, "schemaVersion", config.schemaVersion);

    cJSON* identity = cJSON_AddObjectToObject(root, "identity");
    cJSON* auth = cJSON_AddObjectToObject(root, "auth");
    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON* network = cJSON_AddObjectToObject(root, "network");
    cJSON* callbacks = cJSON_AddObjectToObject(root, "callbacks");
    cJSON* time = cJSON_AddObjectToObject(root, "time");

    if (identity != nullptr) {
        writeIdentity(config.identity, identity);
    }
    if (auth != nullptr) {
        writeAuth(config.auth, auth);
    }
    if (wifi != nullptr) {
        writeWifi(config.wifi, wifi);
    }
    if (network != nullptr) {
        writeNetwork(config.network, network);
    }
    if (callbacks != nullptr) {
        writeCallbacks(config.callbacks, callbacks);
    }
    if (time != nullptr) {
        writeTime(config.time, time);
    }

  cJSON* vad = cJSON_AddObjectToObject(root, "vad");
  if (vad != nullptr) {
    cJSON_AddNumberToObject(vad, "speechStartThreshold", config.vad.speechStartThreshold);
    cJSON_AddNumberToObject(vad, "silenceFinalizeMs", config.vad.silenceFinalizeMs);
    cJSON_AddNumberToObject(vad, "preRollPaddingMs", config.vad.preRollPaddingMs);
    cJSON_AddNumberToObject(vad, "postRollPaddingMs", config.vad.postRollPaddingMs);
  }

    return root;
}

bool jsonToConfig(const cJSON* root, config::AppConfig& config) {
    if (root == nullptr || !cJSON_IsObject(root)) {
        return false;
    }

    const cJSON* schemaVersion = cJSON_GetObjectItemCaseSensitive(root, "schemaVersion");
    if (cJSON_IsNumber(schemaVersion)) {
        config.schemaVersion = static_cast<uint32_t>(schemaVersion->valuedouble);
    }

    const cJSON* identity = cJSON_GetObjectItemCaseSensitive(root, "identity");
    const cJSON* auth = cJSON_GetObjectItemCaseSensitive(root, "auth");
    const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    const cJSON* network = cJSON_GetObjectItemCaseSensitive(root, "network");
    const cJSON* callbacks = cJSON_GetObjectItemCaseSensitive(root, "callbacks");
    const cJSON* time = cJSON_GetObjectItemCaseSensitive(root, "time");
    const cJSON* vad = cJSON_GetObjectItemCaseSensitive(root, "vad");

    if (cJSON_IsObject(identity)) {
        readIdentity(identity, config.identity);
    }
    if (cJSON_IsObject(auth)) {
        readAuth(auth, config.auth);
    }
    if (cJSON_IsObject(wifi)) {
        readWifi(wifi, config.wifi);
    }
    if (cJSON_IsObject(network)) {
        readNetwork(network, config.network);
    }
    if (cJSON_IsObject(callbacks)) {
        readCallbacks(callbacks, config.callbacks);
    }
    if (cJSON_IsObject(time)) {
        readTime(time, config.time);
    }
    if (cJSON_IsObject(vad)) {
        const cJSON* speechStartThreshold = cJSON_GetObjectItemCaseSensitive(vad, "speechStartThreshold");
        const cJSON* silenceFinalizeMs = cJSON_GetObjectItemCaseSensitive(vad, "silenceFinalizeMs");
        const cJSON* preRollPaddingMs = cJSON_GetObjectItemCaseSensitive(vad, "preRollPaddingMs");
        const cJSON* postRollPaddingMs = cJSON_GetObjectItemCaseSensitive(vad, "postRollPaddingMs");
        if (cJSON_IsNumber(speechStartThreshold)) {
            config.vad.speechStartThreshold = static_cast<uint16_t>(speechStartThreshold->valuedouble);
        }
        if (cJSON_IsNumber(silenceFinalizeMs)) {
            config.vad.silenceFinalizeMs = static_cast<uint16_t>(silenceFinalizeMs->valuedouble);
        }
        if (cJSON_IsNumber(preRollPaddingMs)) {
            config.vad.preRollPaddingMs = static_cast<uint16_t>(preRollPaddingMs->valuedouble);
        }
        if (cJSON_IsNumber(postRollPaddingMs)) {
            config.vad.postRollPaddingMs = static_cast<uint16_t>(postRollPaddingMs->valuedouble);
        }
    }

    return true;
}

}  // namespace

bool ConfigStore::loadSaved(config::AppConfig& outConfig) const {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "No config namespace yet: %s", esp_err_to_name(err));
        return false;
    }

    size_t requiredSize = 0;
    err = nvs_get_str(handle, kSavedKey, nullptr, &requiredSize);
    if (err != ESP_OK || requiredSize == 0) {
        nvs_close(handle);
        return false;
    }

    char* jsonBuffer = static_cast<char*>(malloc(requiredSize));
    if (jsonBuffer == nullptr) {
        nvs_close(handle);
        return false;
    }

    err = nvs_get_str(handle, kSavedKey, jsonBuffer, &requiredSize);
    nvs_close(handle);
    if (err != ESP_OK) {
        free(jsonBuffer);
        return false;
    }

    cJSON* root = cJSON_Parse(jsonBuffer);
    free(jsonBuffer);
    if (root == nullptr) {
        ESP_LOGE(kTag, "Failed to parse saved config JSON");
        return false;
    }

    config::AppConfig loaded = config::makeDefaults();
    const bool parsed = jsonToConfig(root, loaded);
    cJSON_Delete(root);
    if (!parsed) {
        return false;
    }

    outConfig = loaded;
    return true;
}

bool ConfigStore::save(const config::AppConfig& config) const {
    cJSON* root = configToJson(config);
    if (root == nullptr) {
        return false;
    }

    char* jsonBuffer = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (jsonBuffer == nullptr) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(jsonBuffer);
        ESP_LOGE(kTag, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, kSavedKey, jsonBuffer);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    free(jsonBuffer);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to save config: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "Saved config to NVS");
    return true;
}

bool ConfigStore::clearSaved() const {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_erase_key(handle, kSavedKey);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}
