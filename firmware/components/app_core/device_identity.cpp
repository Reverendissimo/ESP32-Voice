/**
 * @file device_identity.cpp
 * @brief Implementation of DeviceIdentity.
 */
#include "device_identity.hpp"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"

static const char* kTag = "device_identity";

bool DeviceIdentity::initialize() {
    uint8_t mac[6] = {};
    const esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to read MAC: %s", esp_err_to_name(err));
        return false;
    }

    snprintf(
        m_deviceUid,
        sizeof(m_deviceUid),
        "espbox-%02x%02x%02x%02x%02x%02x",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
    m_initialized = true;
    ESP_LOGI(kTag, "device_uid=%s", m_deviceUid);
    return true;
}

const char* DeviceIdentity::deviceUid() const {
    return m_initialized ? m_deviceUid : "";
}

bool DeviceIdentity::matchesTarget(const char* targetDeviceUid) const {
    if (!m_initialized || targetDeviceUid == nullptr || targetDeviceUid[0] == '\0') {
        return false;
    }
    return strcmp(m_deviceUid, targetDeviceUid) == 0;
}
