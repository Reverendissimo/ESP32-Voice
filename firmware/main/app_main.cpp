/**
 * @file app_main.cpp
 * @brief Firmware entry point for ESP32-S3-BOX-3 voice terminal.
 */
#include "app_bootstrap.hpp"

#include "esp_log.h"

static const char* kTag = "app_main";

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "ESP32-Voice firmware starting");

    AppBootstrap bootstrap;
    if (!bootstrap.start()) {
        ESP_LOGE(kTag, "Bootstrap failed");
        return;
    }

    ESP_LOGI(kTag, "Bootstrap complete");
}
