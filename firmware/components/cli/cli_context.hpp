/**
 * @file cli_context.hpp
 * @brief Shared service pointers for serial CLI command handlers.
 */
#pragma once

class ConfigManager;
class HealthService;
class TimeSyncService;
class WifiManager;

/**
 * @brief Dependencies exposed to CLI handlers.
 */
struct CliContext {
    const char* deviceUid;
    ConfigManager* configManager;
    WifiManager* wifiManager;
    TimeSyncService* timeSyncService;
    HealthService* healthService;
};
