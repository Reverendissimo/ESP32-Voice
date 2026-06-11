/**
 * @file cli_command_registry.cpp
 * @brief Implementation of CliCommandRegistry.
 */
#include "cli_command_registry.hpp"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.hpp"
#include "esp_console.h"
#include "esp_system.h"
#include "health_service.hpp"
#include "time_sync_service.hpp"
#include "wifi_manager.hpp"

static CliContext* s_context = nullptr;

namespace {

void printMaskedSecret(const char* label, const char* value) {
    if (value != nullptr && value[0] != '\0') {
        printf("%s: ***\n", label);
    } else {
        printf("%s: (empty)\n", label);
    }
}

void printConfig(const config::AppConfig& config, bool dirty) {
    printf("schema_version: %lu\n", static_cast<unsigned long>(config.schemaVersion));
    printf("device_name: %s\n", config.identity.deviceName);
    printMaskedSecret("auth_token", config.auth.token);
    printf("wifi_ssid: %s\n", config.wifi.ssid);
    printMaskedSecret("wifi_password", config.wifi.password);
    printf("http_port: %u\n", static_cast<unsigned>(config.network.localHttpPort));
    printf("timezone: %s\n", config.time.timezone);
    printf("sntp_server: %s\n", config.time.sntpServer);
    printf("dirty: %s\n", dirty ? "yes" : "no");
}

bool applyWifiPatch(const char* ssid, const char* password, char* errorOut, size_t errorOutLen) {
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi == nullptr) {
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddStringToObject(wifi, "ssid", ssid);
    cJSON_AddStringToObject(wifi, "password", password);

    char* patchJson = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (patchJson == nullptr) {
        return false;
    }

    const bool ok = s_context->configManager->applyPatchJson(patchJson, errorOut, errorOutLen);
    cJSON_free(patchJson);
    return ok;
}

int cmdHelp(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf(
        "Commands:\n"
        "  help\n"
        "  status\n"
        "  health\n"
        "  wifi_set <ssid> <password>\n"
        "  wifi_test\n"
        "  config_show\n"
        "  config_show_saved\n"
        "  config_load\n"
        "  config_save\n"
        "  config_revert\n"
        "  time_sync\n"
        "  reboot confirm\n"
        "  factory_reset confirm\n");
    return 0;
}

int cmdStatus(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr) {
        printf("CLI not ready\n");
        return 1;
    }

    printf("device_uid: %s\n", s_context->deviceUid != nullptr ? s_context->deviceUid : "");
    if (s_context->wifiManager != nullptr) {
        printf("wifi: %s rssi=%d\n", s_context->wifiManager->stateLabel(), s_context->wifiManager->rssi());
    }
    if (s_context->configManager != nullptr) {
        printf("config_dirty: %s\n", s_context->configManager->isDirty() ? "yes" : "no");
    }
    if (s_context->timeSyncService != nullptr) {
        printf("time_trusted: %s\n", s_context->timeSyncService->isTimeTrusted() ? "yes" : "no");
    }
    return 0;
}

int cmdHealth(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->healthService == nullptr) {
        printf("Health service unavailable\n");
        return 1;
    }

    HealthInputs inputs = {};
    inputs.deviceUid = s_context->deviceUid;
    if (s_context->configManager != nullptr) {
        inputs.deviceName = s_context->configManager->active().identity.deviceName;
        inputs.configDirty = s_context->configManager->isDirty();
    }
    if (s_context->wifiManager != nullptr) {
        inputs.wifiState = s_context->wifiManager->stateLabel();
        inputs.rssi = s_context->wifiManager->rssi();
    }
    if (s_context->timeSyncService != nullptr) {
        inputs.timeTrusted = s_context->timeSyncService->isTimeTrusted();
    }

    HealthSnapshot snapshot = {};
    if (!s_context->healthService->collect(inputs, snapshot)) {
        printf("Failed to collect health\n");
        return 1;
    }

    printf("device_uid: %s\n", snapshot.deviceUid);
    printf("device_name: %s\n", snapshot.deviceName);
    printf("firmware_version: %s\n", snapshot.firmwareVersion);
    printf("api_version: %s\n", snapshot.apiVersion);
    printf("main_state: %s\n", snapshot.mainState);
    printf("wifi_state: %s\n", snapshot.wifiState);
    printf("rssi: %d\n", snapshot.rssi);
    printf("uptime_ms: %llu\n", static_cast<unsigned long long>(snapshot.uptimeMs));
    printf("free_heap: %lu\n", static_cast<unsigned long>(snapshot.freeHeap));
    printf("battery_percent: %d\n", snapshot.batteryPercent);
    printf("time_trusted: %s\n", snapshot.timeTrusted ? "yes" : "no");
    printf("config_dirty: %s\n", snapshot.configDirty ? "yes" : "no");
    return 0;
}

int cmdWifiSet(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: wifi_set <ssid> <password>\n");
        return 1;
    }

    char error[128] = {};
    if (!applyWifiPatch(argv[1], argv[2], error, sizeof(error))) {
        printf("wifi_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }

    printf("Wi-Fi credentials updated in active config (not saved until config_save)\n");
    return 0;
}

int cmdWifiTest(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr || s_context->wifiManager == nullptr) {
        printf("Wi-Fi services unavailable\n");
        return 1;
    }

    const config::WifiConfig& wifi = s_context->configManager->active().wifi;
    if (wifi.ssid[0] == '\0') {
        printf("No SSID in active config\n");
        return 1;
    }

    const bool ok = s_context->wifiManager->testCredentials(wifi.ssid, wifi.password, 15000);
    printf("wifi_test: %s\n", ok ? "connected" : "failed");
    return ok ? 0 : 1;
}

int cmdConfigShow(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    printf("[active config]\n");
    printConfig(s_context->configManager->active(), s_context->configManager->isDirty());
    return 0;
}

int cmdConfigShowSaved(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    printf("[saved config]\n");
    printConfig(s_context->configManager->saved(), false);
    return 0;
}

int cmdConfigLoad(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    if (!s_context->configManager->loadSavedIntoActive()) {
        printf("config_load failed: no saved config\n");
        return 1;
    }
    printf("Loaded saved config into active RAM\n");
    return 0;
}

int cmdConfigSave(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    if (!s_context->configManager->saveActive()) {
        printf("config_save failed\n");
        return 1;
    }
    printf("Active config saved to NVS\n");
    return 0;
}

int cmdConfigRevert(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    s_context->configManager->revertActive();
    printf("Reverted active config to saved/default baseline\n");
    return 0;
}

int cmdTimeSync(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->timeSyncService == nullptr) {
        return 1;
    }
    const bool ok = s_context->timeSyncService->syncNow();
    printf("time_sync: %s (trusted=%s)\n", ok ? "ok" : "failed", s_context->timeSyncService->isTimeTrusted() ? "yes" : "no");
    return ok ? 0 : 1;
}

int cmdReboot(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "confirm") != 0) {
        printf("This will reboot the device. Run: reboot confirm\n");
        return 1;
    }
    printf("Rebooting...\n");
    esp_restart();
    return 0;
}

int cmdFactoryReset(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "confirm") != 0) {
        printf("This will erase saved config. Run: factory_reset confirm\n");
        return 1;
    }
    if (s_context != nullptr && s_context->configManager != nullptr) {
        s_context->configManager->resetSaved();
    }
    printf("Factory reset complete. Rebooting...\n");
    esp_restart();
    return 0;
}

bool registerCommand(const char* name, const char* help, esp_console_cmd_func_t func) {
    const esp_console_cmd_t cmd = {
        .command = name,
        .help = help,
        .hint = nullptr,
        .func = func,
    };
    return esp_console_cmd_register(&cmd) == ESP_OK;
}

}  // namespace

bool CliCommandRegistry::registerCommands(const CliContext* context) const {
    if (context == nullptr) {
        return false;
    }

    s_context = const_cast<CliContext*>(context);

    return registerCommand("help", "Show available commands", cmdHelp) &&
           registerCommand("status", "Show brief device status", cmdStatus) &&
           registerCommand("health", "Show health snapshot", cmdHealth) &&
           registerCommand("wifi_set", "wifi_set <ssid> <password>", cmdWifiSet) &&
           registerCommand("wifi_test", "Test active Wi-Fi credentials", cmdWifiTest) &&
           registerCommand("config_show", "Show active config (masked)", cmdConfigShow) &&
           registerCommand("config_show_saved", "Show saved config (masked)", cmdConfigShowSaved) &&
           registerCommand("config_load", "Load saved config into active RAM", cmdConfigLoad) &&
           registerCommand("config_save", "Persist active config to NVS", cmdConfigSave) &&
           registerCommand("config_revert", "Revert active config changes", cmdConfigRevert) &&
           registerCommand("time_sync", "Trigger SNTP sync", cmdTimeSync) &&
           registerCommand("reboot", "Reboot device (use: reboot confirm)", cmdReboot) &&
           registerCommand("factory_reset", "Erase saved config (use: factory_reset confirm)", cmdFactoryReset);
}
