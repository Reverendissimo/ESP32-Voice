/**
 * @file config_validator.cpp
 * @brief Implementation of ConfigValidator.
 */
#include "config_validator.hpp"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

namespace {

void setError(char* errorOut, size_t errorOutLen, const char* message) {
    if (errorOut == nullptr || errorOutLen == 0) {
        return;
    }
    strncpy(errorOut, message, errorOutLen - 1);
    errorOut[errorOutLen - 1] = '\0';
}

bool isStringField(const cJSON* object, const char* fieldName) {
    const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, fieldName);
    return field == nullptr || cJSON_IsString(field);
}

bool isNumberField(const cJSON* object, const char* fieldName) {
    const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, fieldName);
    return field == nullptr || cJSON_IsNumber(field);
}

bool isBoolField(const cJSON* object, const char* fieldName) {
    const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, fieldName);
    return field == nullptr || cJSON_IsBool(field);
}

}  // namespace

bool ConfigValidator::validateConfig(const config::AppConfig& config, char* errorOut, size_t errorOutLen) const {
    if (config.schemaVersion != config::kSchemaVersion) {
        setError(errorOut, errorOutLen, "Unsupported schema version");
        return false;
    }

    if (config.network.localHttpPort == 0) {
        setError(errorOut, errorOutLen, "network.localHttpPort must be > 0");
        return false;
    }

    if (config.batteryAlarm.enabled &&
        config.batteryAlarm.lowClearPercent <= config.batteryAlarm.lowTriggerPercent) {
        setError(errorOut, errorOutLen, "batteryAlarm clear must be above trigger");
        return false;
    }

    if (config.environmentAlarm.highEnabled &&
        config.environmentAlarm.highClearC >= config.environmentAlarm.highTriggerC) {
        setError(errorOut, errorOutLen, "environmentAlarm high clear must be below trigger");
        return false;
    }

    if (config.environmentAlarm.lowEnabled &&
        config.environmentAlarm.lowClearC <= config.environmentAlarm.lowTriggerC) {
        setError(errorOut, errorOutLen, "environmentAlarm low clear must be above trigger");
        return false;
    }

    if (config.vad.silenceFinalizeMs < 200) {
        setError(errorOut, errorOutLen, "vad.silenceFinalizeMs too small");
        return false;
    }

    return true;
}

bool ConfigValidator::validatePatch(const cJSON* patch, char* errorOut, size_t errorOutLen) const {
    if (patch == nullptr || !cJSON_IsObject(patch)) {
        setError(errorOut, errorOutLen, "Patch must be a JSON object");
        return false;
    }

    const cJSON* identity = cJSON_GetObjectItemCaseSensitive(patch, "identity");
    if (identity != nullptr) {
        if (!cJSON_IsObject(identity) || !isStringField(identity, "deviceName")) {
            setError(errorOut, errorOutLen, "Invalid identity patch");
            return false;
        }
    }

    const cJSON* auth = cJSON_GetObjectItemCaseSensitive(patch, "auth");
    if (auth != nullptr) {
        if (!cJSON_IsObject(auth) || !isStringField(auth, "token")) {
            setError(errorOut, errorOutLen, "Invalid auth patch");
            return false;
        }
    }

    const cJSON* vad = cJSON_GetObjectItemCaseSensitive(patch, "vad");
    if (vad != nullptr) {
        if (!cJSON_IsObject(vad) || !isNumberField(vad, "speechStartThreshold") ||
            !isNumberField(vad, "silenceFinalizeMs")) {
            setError(errorOut, errorOutLen, "Invalid vad patch");
            return false;
        }
    }

    const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(patch, "wifi");
    if (wifi != nullptr) {
        if (!cJSON_IsObject(wifi) || !isStringField(wifi, "ssid") || !isStringField(wifi, "password")) {
            setError(errorOut, errorOutLen, "Invalid wifi patch");
            return false;
        }
    }

    const cJSON* callbacks = cJSON_GetObjectItemCaseSensitive(patch, "callbacks");
    if (callbacks != nullptr) {
        if (!cJSON_IsObject(callbacks) || !isStringField(callbacks, "speechUrl") ||
            !isStringField(callbacks, "speechFinalizeUrl") || !isStringField(callbacks, "uiEventUrl") ||
            !isStringField(callbacks, "heartbeatUrl")) {
            setError(errorOut, errorOutLen, "Invalid callbacks patch");
            return false;
        }
    }

    const cJSON* network = cJSON_GetObjectItemCaseSensitive(patch, "network");
    if (network != nullptr) {
        if (!cJSON_IsObject(network) || !isStringField(network, "callbackBaseUrl") ||
            !isNumberField(network, "localHttpPort")) {
            setError(errorOut, errorOutLen, "Invalid network patch");
            return false;
        }
    }

    const cJSON* time = cJSON_GetObjectItemCaseSensitive(patch, "time");
    if (time != nullptr) {
        if (!cJSON_IsObject(time) || !isStringField(time, "timezone") || !isStringField(time, "sntpServer") ||
            !isNumberField(time, "syncIntervalSec")) {
            setError(errorOut, errorOutLen, "Invalid time patch");
            return false;
        }
    }

    const cJSON* batteryAlarm = cJSON_GetObjectItemCaseSensitive(patch, "batteryAlarm");
    if (batteryAlarm != nullptr) {
        if (!cJSON_IsObject(batteryAlarm) || !isBoolField(batteryAlarm, "enabled") ||
            !isNumberField(batteryAlarm, "lowTriggerPercent") ||
            !isNumberField(batteryAlarm, "lowClearPercent")) {
            setError(errorOut, errorOutLen, "Invalid batteryAlarm patch");
            return false;
        }
    }

    return true;
}
