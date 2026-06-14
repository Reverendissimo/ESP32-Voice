/**
 * @file app_main.cpp
 * @brief Firmware entry point for ESP32-S3-BOX-3 voice terminal.
 */
#include "app_bootstrap.hpp"
#include "ota_service.hpp"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* kTag = "app_main";

extern "C" void app_main(void) {
    OtaService::validateRunningImageOnBoot();

    const esp_app_desc_t* appDesc = esp_app_get_description();
    ESP_LOGI(
        kTag,
        "ESP32-Voice starting version=%s",
        appDesc != nullptr ? appDesc->version : "unknown");

    static AppBootstrap bootstrap;
    if (!bootstrap.start()) {
        ESP_LOGE(kTag, "Bootstrap incomplete — serial CLI may still work; type help");
    } else {
        ESP_LOGI(kTag, "Bootstrap complete");
    }

    // Never return: destroying bootstrap while FreeRTOS tasks run corrupts the heap.
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
