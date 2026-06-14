/**
 * @file wifi_manager.cpp
 * @brief Implementation of WifiManager.
 */
#include "wifi_manager.hpp"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* kTag = "wifi_manager";

static constexpr int kConnectedBit = BIT0;
static EventGroupHandle_t s_wifiEventGroup = nullptr;

namespace {

void onWifiEvent(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    (void)arg;
    (void)eventBase;
    (void)eventData;

    if (eventId == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (eventId == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifiEventGroup != nullptr) {
            xEventGroupClearBits(s_wifiEventGroup, kConnectedBit);
        }
        esp_wifi_connect();
    }
}

void onIpEvent(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    (void)arg;
    (void)eventBase;
    if (eventId == IP_EVENT_STA_GOT_IP && s_wifiEventGroup != nullptr) {
        xEventGroupSetBits(s_wifiEventGroup, kConnectedBit);
    }
}

bool configureStation(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        ESP_LOGW(kTag, "Skipping Wi-Fi connect: SSID not configured");
        return false;
    }

    wifi_config_t wifiConfig = {};
    strncpy(reinterpret_cast<char*>(wifiConfig.sta.ssid), ssid, sizeof(wifiConfig.sta.ssid) - 1);
    if (password != nullptr) {
        strncpy(reinterpret_cast<char*>(wifiConfig.sta.password), password, sizeof(wifiConfig.sta.password) - 1);
    }
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
    // MIN_MODEM avoids wifi:m f null under sustained HTTP load (IDF Wi-Fi PS quirk).
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_wifi_start());
    return true;
}

}  // namespace

bool WifiManager::initialize() {
    if (m_initialized) {
        return true;
    }

    s_wifiEventGroup = xEventGroupCreate();
    if (s_wifiEventGroup == nullptr) {
        ESP_LOGE(kTag, "Failed to create Wi-Fi event group");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onIpEvent, nullptr));

    m_initialized = true;
    ESP_LOGI(kTag, "Wi-Fi manager initialized");
    return true;
}

bool WifiManager::applyConfig(const config::WifiConfig& wifiConfig) {
    if (!m_initialized) {
        return false;
    }

    if (!configureStation(wifiConfig.ssid, wifiConfig.password)) {
        m_connected = false;
        return true;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifiEventGroup, kConnectedBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    m_connected = (bits & kConnectedBit) != 0;

    if (m_connected) {
        wifi_ap_record_t apInfo = {};
        if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK) {
            m_rssi = apInfo.rssi;
        }
        ESP_LOGI(kTag, "Connected to %s rssi=%d", wifiConfig.ssid, m_rssi);
    } else {
        ESP_LOGW(kTag, "Wi-Fi connect timeout for ssid=%s", wifiConfig.ssid);
    }

    return m_connected;
}

bool WifiManager::testCredentials(const char* ssid, const char* password, uint32_t timeoutMs) {
    if (!m_initialized || ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    ESP_ERROR_CHECK(esp_wifi_stop());
    xEventGroupClearBits(s_wifiEventGroup, kConnectedBit);

    if (!configureStation(ssid, password)) {
        return false;
    }

    const EventBits_t bits =
        xEventGroupWaitBits(s_wifiEventGroup, kConnectedBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  const bool connected = (bits & kConnectedBit) != 0;
  ESP_LOGI(kTag, "Wi-Fi test ssid=%s connected=%d", ssid, connected);
  return connected;
}

bool WifiManager::isConnected() const {
    return m_connected;
}

int8_t WifiManager::rssi() const {
    return m_rssi;
}

const char* WifiManager::stateLabel() const {
    return m_connected ? "connected" : "disconnected";
}
