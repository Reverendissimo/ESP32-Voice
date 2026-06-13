/**
 * @file json_response_builder.cpp
 * @brief Implementation of JsonResponseBuilder.
 */
#include "json_response_builder.hpp"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"

namespace {

const char* kApiVersion = "1";
const char* kMaskedSecret = "***";

void addConfigObject(cJSON* configObject, const config::AppConfig& config, bool maskSecrets) {
    cJSON* identity = cJSON_AddObjectToObject(configObject, "identity");
    cJSON* auth = cJSON_AddObjectToObject(configObject, "auth");
    cJSON* wifi = cJSON_AddObjectToObject(configObject, "wifi");
    cJSON* network = cJSON_AddObjectToObject(configObject, "network");
    cJSON* callbacks = cJSON_AddObjectToObject(configObject, "callbacks");
    cJSON* vad = cJSON_AddObjectToObject(configObject, "vad");
    cJSON* time = cJSON_AddObjectToObject(configObject, "time");

    if (identity != nullptr) {
        cJSON_AddStringToObject(identity, "deviceName", config.identity.deviceName);
    }
    if (auth != nullptr) {
        cJSON_AddStringToObject(
            auth,
            "token",
            (maskSecrets && config.auth.token[0] != '\0') ? kMaskedSecret : config.auth.token);
    }
    if (wifi != nullptr) {
        cJSON_AddStringToObject(wifi, "ssid", config.wifi.ssid);
        cJSON_AddStringToObject(
            wifi,
            "password",
            (maskSecrets && config.wifi.password[0] != '\0') ? kMaskedSecret : config.wifi.password);
    }
    if (network != nullptr) {
        cJSON_AddStringToObject(network, "callbackBaseUrl", config.network.callbackBaseUrl);
        cJSON_AddNumberToObject(network, "localHttpPort", config.network.localHttpPort);
    }
    if (callbacks != nullptr) {
        cJSON_AddStringToObject(callbacks, "speechUrl", config.callbacks.speechUrl);
        cJSON_AddStringToObject(callbacks, "speechFinalizeUrl", config.callbacks.speechFinalizeUrl);
        cJSON_AddStringToObject(callbacks, "uiEventUrl", config.callbacks.uiEventUrl);
        cJSON_AddStringToObject(callbacks, "heartbeatUrl", config.callbacks.heartbeatUrl);
    }
    if (vad != nullptr) {
        cJSON_AddNumberToObject(vad, "speechStartThreshold", config.vad.speechStartThreshold);
        cJSON_AddNumberToObject(vad, "silenceFinalizeMs", config.vad.silenceFinalizeMs);
    }
    if (time != nullptr) {
        cJSON_AddStringToObject(time, "timezone", config.time.timezone);
        cJSON_AddStringToObject(time, "sntpServer", config.time.sntpServer);
        cJSON_AddNumberToObject(time, "syncIntervalSec", config.time.syncIntervalSec);
    }

    cJSON_AddNumberToObject(configObject, "schemaVersion", config.schemaVersion);
}

}  // namespace

bool JsonResponseBuilder::buildHealth(
    const char* deviceUid,
    const HealthSnapshot& snapshot,
    char* outBuffer,
    size_t outBufferLen) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "device_uid", deviceUid != nullptr ? deviceUid : "");
    cJSON_AddStringToObject(root, "device_name", snapshot.deviceName);
    cJSON_AddStringToObject(root, "firmware_version", snapshot.firmwareVersion);
    cJSON_AddStringToObject(root, "api_version", snapshot.apiVersion);
    cJSON_AddNumberToObject(root, "uptime_ms", static_cast<double>(snapshot.uptimeMs));
    cJSON_AddStringToObject(root, "wifi_state", snapshot.wifiState);
    cJSON_AddNumberToObject(root, "rssi", snapshot.rssi);
    cJSON_AddNumberToObject(root, "free_heap", snapshot.freeHeap);
    cJSON_AddStringToObject(root, "main_state", snapshot.mainState);
    cJSON_AddNumberToObject(root, "battery_percent", snapshot.batteryPercent);
    cJSON_AddBoolToObject(root, "time_trusted", snapshot.timeTrusted);
    cJSON_AddBoolToObject(root, "config_dirty", snapshot.configDirty);

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == nullptr) {
        return false;
    }

    strncpy(outBuffer, printed, outBufferLen - 1);
    outBuffer[outBufferLen - 1] = '\0';
    cJSON_free(printed);
    return true;
}

bool JsonResponseBuilder::buildVersion(const char* deviceUid, char* outBuffer, size_t outBufferLen) const {
    const esp_app_desc_t* appDesc = esp_app_get_description();
    const char* firmwareVersion = (appDesc != nullptr) ? appDesc->version : "";

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "device_uid", deviceUid != nullptr ? deviceUid : "");
    cJSON_AddStringToObject(root, "firmware_version", firmwareVersion);
    cJSON_AddStringToObject(root, "api_version", kApiVersion);

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == nullptr) {
        return false;
    }

    strncpy(outBuffer, printed, outBufferLen - 1);
    outBuffer[outBufferLen - 1] = '\0';
    cJSON_free(printed);
    return true;
}

bool JsonResponseBuilder::buildConfig(
    const char* deviceUid,
    const config::AppConfig& config,
    bool dirty,
    bool maskSecrets,
    char* outBuffer,
    size_t outBufferLen) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "device_uid", deviceUid != nullptr ? deviceUid : "");
    cJSON_AddBoolToObject(root, "dirty", dirty);

    cJSON* configObject = cJSON_AddObjectToObject(root, "config");
    if (configObject != nullptr) {
        addConfigObject(configObject, config, maskSecrets);
    }

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed == nullptr) {
        return false;
    }

    strncpy(outBuffer, printed, outBufferLen - 1);
    outBuffer[outBufferLen - 1] = '\0';
    cJSON_free(printed);
    return true;
}
