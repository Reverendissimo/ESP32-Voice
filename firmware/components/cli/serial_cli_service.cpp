/**
 * @file serial_cli_service.cpp
 * @brief Implementation of SerialCliService.
 */
#include "serial_cli_service.hpp"

#include "cli_command_registry.hpp"
#include "esp_console.h"
#include "esp_log.h"

static const char* kTag = "serial_cli";

bool SerialCliService::start(const CliContext* context) {
    if (m_running || context == nullptr) {
        return false;
    }

    esp_console_repl_config_t replConfig = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    replConfig.prompt = "esp32-voice> ";
    replConfig.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t hwConfig = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_t* repl = nullptr;

    const esp_err_t createErr = esp_console_new_repl_usb_serial_jtag(&hwConfig, &replConfig, &repl);
    if (createErr != ESP_OK) {
        ESP_LOGE(kTag, "Failed to create USB serial CLI: %s", esp_err_to_name(createErr));
        return false;
    }

    CliCommandRegistry registry;
    if (!registry.registerCommands(context)) {
        ESP_LOGE(kTag, "Failed to register CLI commands");
        return false;
    }

    const esp_err_t startErr = esp_console_start_repl(repl);
    if (startErr != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start CLI REPL: %s", esp_err_to_name(startErr));
        return false;
    }

    m_running = true;
    ESP_LOGI(kTag, "USB serial CLI started");
    return true;
}

bool SerialCliService::isRunning() const {
    return m_running;
}
