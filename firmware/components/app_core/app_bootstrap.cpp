/**
 * @file app_bootstrap.cpp
 * @brief Implementation of AppBootstrap.
 */
#include "app_bootstrap.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

static const char* kTag = "app_bootstrap";

bool AppBootstrap::initializeNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool AppBootstrap::startHttpServer() {
    const config::AppConfig& activeConfig = m_configManager.active();
    m_authContext.configure(activeConfig.auth.token);

    m_apiContext.deviceUid = m_identity.deviceUid();
    m_apiContext.configManager = &m_configManager;
    m_apiContext.wifiManager = &m_wifiManager;
    m_apiContext.timeSyncService = &m_timeSyncService;
    m_apiContext.healthService = &m_healthService;
    m_apiContext.authContext = &m_authContext;

    if (!m_httpServer.start(activeConfig.network.localHttpPort, &m_apiContext)) {
        ESP_LOGE(kTag, "HTTP server failed to start");
        return false;
    }
    return true;
}

bool AppBootstrap::start() {
    if (!initializeNvs()) {
        return false;
    }

    if (!m_identity.initialize()) {
        return false;
    }

    if (!m_configManager.initialize()) {
        return false;
    }

    if (!m_wifiManager.initialize()) {
        return false;
    }

    const config::AppConfig& activeConfig = m_configManager.active();
    if (activeConfig.wifi.ssid[0] != '\0') {
        m_wifiManager.applyConfig(activeConfig.wifi);
    }

    if (!m_timeSyncService.start(activeConfig.time)) {
        ESP_LOGW(kTag, "SNTP start failed");
    }

    if (!startHttpServer()) {
        return false;
    }

    HealthInputs inputs = {};
    inputs.deviceUid = m_identity.deviceUid();
    inputs.deviceName = activeConfig.identity.deviceName;
    inputs.wifiState = m_wifiManager.stateLabel();
    inputs.rssi = m_wifiManager.rssi();
    inputs.timeTrusted = m_timeSyncService.isTimeTrusted();
    inputs.configDirty = m_configManager.isDirty();

    HealthSnapshot health = {};
    if (m_healthService.collect(inputs, health)) {
        ESP_LOGI(
            kTag,
            "ready uid=%s wifi=%s heap=%lu dirty=%d time_trusted=%d http_port=%u",
            health.deviceUid,
            health.wifiState,
            static_cast<unsigned long>(health.freeHeap),
            health.configDirty,
            health.timeTrusted,
            static_cast<unsigned>(activeConfig.network.localHttpPort));
    }

    return true;
}

const DeviceIdentity& AppBootstrap::identity() const {
    return m_identity;
}

const ConfigManager& AppBootstrap::configManager() const {
    return m_configManager;
}

const WifiManager& AppBootstrap::wifiManager() const {
    return m_wifiManager;
}

const TimeSyncService& AppBootstrap::timeSyncService() const {
    return m_timeSyncService;
}

const HealthService& AppBootstrap::healthService() const {
    return m_healthService;
}

const HttpServerService& AppBootstrap::httpServer() const {
    return m_httpServer;
}
