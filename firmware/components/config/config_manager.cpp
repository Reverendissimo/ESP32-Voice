/**
 * @file config_manager.cpp
 * @brief Implementation of ConfigManager.
 */
#include "config_manager.hpp"

#include <string.h>

#include "cJSON.h"
#include "config_store.hpp"
#include "config_validator.hpp"
#include "esp_log.h"

static const char* kTag = "config_manager";

namespace {

void copyString(char* dest, size_t destSize, const char* value) {
    if (value == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, destSize - 1);
    dest[destSize - 1] = '\0';
}

}  // namespace

bool ConfigManager::initialize() {
    m_defaults = config::makeDefaults();
    m_saved = m_defaults;
    m_active = m_defaults;
    m_hasSaved = m_store.loadSaved(m_saved);
    if (m_hasSaved) {
        m_active = m_saved;
        ESP_LOGI(kTag, "Loaded saved config from NVS");
    } else {
        ESP_LOGI(kTag, "Using factory defaults (no saved config)");
    }
    updateDirtyFlag();
    return true;
}

bool ConfigManager::applyPatchJson(const char* patchJson, char* errorOut, size_t errorOutLen) {
    if (patchJson == nullptr) {
        if (errorOut != nullptr && errorOutLen > 0) {
            strncpy(errorOut, "Missing patch JSON", errorOutLen - 1);
            errorOut[errorOutLen - 1] = '\0';
        }
        return false;
    }

    cJSON* patch = cJSON_Parse(patchJson);
    if (patch == nullptr) {
        if (errorOut != nullptr && errorOutLen > 0) {
            strncpy(errorOut, "Invalid JSON", errorOutLen - 1);
            errorOut[errorOutLen - 1] = '\0';
        }
        return false;
    }

    const bool applied = applyPatchObject(patch, errorOut, errorOutLen);
    cJSON_Delete(patch);
    return applied;
}

bool ConfigManager::applyPatchObject(const cJSON* patch, char* errorOut, size_t errorOutLen) {
    if (!m_validator.validatePatch(patch, errorOut, errorOutLen)) {
        return false;
    }

    config::AppConfig candidate = m_active;

    const cJSON* identity = cJSON_GetObjectItemCaseSensitive(patch, "identity");
    if (cJSON_IsObject(identity)) {
        const cJSON* deviceName = cJSON_GetObjectItemCaseSensitive(identity, "deviceName");
        if (cJSON_IsString(deviceName)) {
            copyString(candidate.identity.deviceName, sizeof(candidate.identity.deviceName), deviceName->valuestring);
        }
    }

    const cJSON* auth = cJSON_GetObjectItemCaseSensitive(patch, "auth");
    if (cJSON_IsObject(auth)) {
        const cJSON* token = cJSON_GetObjectItemCaseSensitive(auth, "token");
        if (cJSON_IsString(token)) {
            copyString(candidate.auth.token, sizeof(candidate.auth.token), token->valuestring);
        }
    }

    const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(patch, "wifi");
    if (cJSON_IsObject(wifi)) {
        const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        const cJSON* password = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (cJSON_IsString(ssid)) {
            copyString(candidate.wifi.ssid, sizeof(candidate.wifi.ssid), ssid->valuestring);
        }
        if (cJSON_IsString(password)) {
            copyString(candidate.wifi.password, sizeof(candidate.wifi.password), password->valuestring);
        }
    }

    const cJSON* callbacks = cJSON_GetObjectItemCaseSensitive(patch, "callbacks");
    if (cJSON_IsObject(callbacks)) {
        const cJSON* speechUrl = cJSON_GetObjectItemCaseSensitive(callbacks, "speechUrl");
        const cJSON* speechFinalizeUrl = cJSON_GetObjectItemCaseSensitive(callbacks, "speechFinalizeUrl");
        const cJSON* uiEventUrl = cJSON_GetObjectItemCaseSensitive(callbacks, "uiEventUrl");
        const cJSON* heartbeatUrl = cJSON_GetObjectItemCaseSensitive(callbacks, "heartbeatUrl");
        if (cJSON_IsString(speechUrl)) {
            copyString(candidate.callbacks.speechUrl, sizeof(candidate.callbacks.speechUrl), speechUrl->valuestring);
        }
        if (cJSON_IsString(speechFinalizeUrl)) {
            copyString(
                candidate.callbacks.speechFinalizeUrl,
                sizeof(candidate.callbacks.speechFinalizeUrl),
                speechFinalizeUrl->valuestring);
        }
        if (cJSON_IsString(uiEventUrl)) {
            copyString(candidate.callbacks.uiEventUrl, sizeof(candidate.callbacks.uiEventUrl), uiEventUrl->valuestring);
        }
        if (cJSON_IsString(heartbeatUrl)) {
            copyString(
                candidate.callbacks.heartbeatUrl, sizeof(candidate.callbacks.heartbeatUrl), heartbeatUrl->valuestring);
        }
    }

    const cJSON* network = cJSON_GetObjectItemCaseSensitive(patch, "network");
    if (cJSON_IsObject(network)) {
        const cJSON* callbackBaseUrl = cJSON_GetObjectItemCaseSensitive(network, "callbackBaseUrl");
        const cJSON* localHttpPort = cJSON_GetObjectItemCaseSensitive(network, "localHttpPort");
        if (cJSON_IsString(callbackBaseUrl)) {
            copyString(
                candidate.network.callbackBaseUrl,
                sizeof(candidate.network.callbackBaseUrl),
                callbackBaseUrl->valuestring);
        }
        if (cJSON_IsNumber(localHttpPort)) {
            candidate.network.localHttpPort = static_cast<uint16_t>(localHttpPort->valuedouble);
        }
    }

    const cJSON* vad = cJSON_GetObjectItemCaseSensitive(patch, "vad");
    if (cJSON_IsObject(vad)) {
        const cJSON* speechStartThreshold = cJSON_GetObjectItemCaseSensitive(vad, "speechStartThreshold");
        const cJSON* silenceFinalizeMs = cJSON_GetObjectItemCaseSensitive(vad, "silenceFinalizeMs");
        const cJSON* preRollPaddingMs = cJSON_GetObjectItemCaseSensitive(vad, "preRollPaddingMs");
        const cJSON* postRollPaddingMs = cJSON_GetObjectItemCaseSensitive(vad, "postRollPaddingMs");
        if (cJSON_IsNumber(speechStartThreshold)) {
            candidate.vad.speechStartThreshold = static_cast<uint16_t>(speechStartThreshold->valuedouble);
        }
        if (cJSON_IsNumber(silenceFinalizeMs)) {
            candidate.vad.silenceFinalizeMs = static_cast<uint16_t>(silenceFinalizeMs->valuedouble);
        }
        if (cJSON_IsNumber(preRollPaddingMs)) {
            candidate.vad.preRollPaddingMs = static_cast<uint16_t>(preRollPaddingMs->valuedouble);
        }
        if (cJSON_IsNumber(postRollPaddingMs)) {
            candidate.vad.postRollPaddingMs = static_cast<uint16_t>(postRollPaddingMs->valuedouble);
        }
    }

    const cJSON* time = cJSON_GetObjectItemCaseSensitive(patch, "time");
    if (cJSON_IsObject(time)) {
        const cJSON* timezone = cJSON_GetObjectItemCaseSensitive(time, "timezone");
        const cJSON* sntpServer = cJSON_GetObjectItemCaseSensitive(time, "sntpServer");
        const cJSON* syncIntervalSec = cJSON_GetObjectItemCaseSensitive(time, "syncIntervalSec");
        if (cJSON_IsString(timezone)) {
            copyString(candidate.time.timezone, sizeof(candidate.time.timezone), timezone->valuestring);
        }
        if (cJSON_IsString(sntpServer)) {
            copyString(candidate.time.sntpServer, sizeof(candidate.time.sntpServer), sntpServer->valuestring);
        }
        if (cJSON_IsNumber(syncIntervalSec)) {
            candidate.time.syncIntervalSec = static_cast<uint32_t>(syncIntervalSec->valuedouble);
        }
    }

    if (!m_validator.validateConfig(candidate, errorOut, errorOutLen)) {
        return false;
    }

    m_active = candidate;
    updateDirtyFlag();
    return true;
}

bool ConfigManager::saveActive() {
    if (!m_validator.validateConfig(m_active, nullptr, 0)) {
        return false;
    }
    if (!m_store.save(m_active)) {
        return false;
    }
    m_saved = m_active;
    m_hasSaved = true;
    updateDirtyFlag();
    return true;
}

bool ConfigManager::loadSavedIntoActive() {
    config::AppConfig loaded = m_defaults;
    if (!m_store.loadSaved(loaded)) {
        return false;
    }
    if (!m_validator.validateConfig(loaded, nullptr, 0)) {
        return false;
    }
    m_saved = loaded;
    m_active = loaded;
    m_hasSaved = true;
    updateDirtyFlag();
    return true;
}

void ConfigManager::revertActive() {
    m_active = m_hasSaved ? m_saved : m_defaults;
    updateDirtyFlag();
}

void ConfigManager::resetActiveToDefaults() {
    m_active = m_defaults;
    updateDirtyFlag();
}

bool ConfigManager::resetSaved() {
    if (!m_store.clearSaved()) {
        return false;
    }
    m_hasSaved = false;
    m_saved = m_defaults;
    m_active = m_defaults;
    updateDirtyFlag();
    return true;
}

bool ConfigManager::isDirty() const {
    return m_dirty;
}

const config::AppConfig& ConfigManager::defaults() const {
    return m_defaults;
}

const config::AppConfig& ConfigManager::saved() const {
    return m_saved;
}

const config::AppConfig& ConfigManager::active() const {
    return m_active;
}

bool ConfigManager::configsEqual(const config::AppConfig& left, const config::AppConfig& right) const {
    return memcmp(&left, &right, sizeof(config::AppConfig)) == 0;
}

void ConfigManager::updateDirtyFlag() {
    const config::AppConfig& baseline = m_hasSaved ? m_saved : m_defaults;
    m_dirty = !configsEqual(m_active, baseline);
}
