/**
 * @file health_service.cpp
 * @brief Implementation of HealthService.
 */
#include "health_service.hpp"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char* kApiVersion = "1";

bool HealthService::collect(const HealthInputs& inputs, HealthSnapshot& outSnapshot) const {
    memset(&outSnapshot, 0, sizeof(outSnapshot));

    if (inputs.deviceUid != nullptr) {
        strncpy(outSnapshot.deviceUid, inputs.deviceUid, sizeof(outSnapshot.deviceUid) - 1);
    }
    if (inputs.deviceName != nullptr) {
        strncpy(outSnapshot.deviceName, inputs.deviceName, sizeof(outSnapshot.deviceName) - 1);
    }
    if (inputs.wifiState != nullptr) {
        strncpy(outSnapshot.wifiState, inputs.wifiState, sizeof(outSnapshot.wifiState) - 1);
    }

    const esp_app_desc_t* appDesc = esp_app_get_description();
    if (appDesc != nullptr) {
        strncpy(outSnapshot.firmwareVersion, appDesc->version, sizeof(outSnapshot.firmwareVersion) - 1);
    }

    strncpy(outSnapshot.apiVersion, kApiVersion, sizeof(outSnapshot.apiVersion) - 1);
    strncpy(outSnapshot.mainState, "running", sizeof(outSnapshot.mainState) - 1);

    outSnapshot.uptimeMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    outSnapshot.freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    outSnapshot.rssi = inputs.rssi;
    outSnapshot.batteryPercent = -1;
    outSnapshot.timeTrusted = inputs.timeTrusted;
    outSnapshot.configDirty = inputs.configDirty;

    return true;
}
