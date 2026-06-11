/**
 * @file api_context.hpp
 * @brief Shared service pointers for REST route handlers.
 *
 * Responsibilities:
 * - pass dependencies into thin HTTP handlers
 *
 * Non-responsibilities:
 * - business logic
 * - service lifetime management
 */
#pragma once

class AuthContext;
class ConfigManager;
class HealthService;
class TimeSyncService;
class WifiManager;

/**
 * @brief Dependencies exposed to REST handlers.
 */
struct ApiContext {
    const char* deviceUid;
    ConfigManager* configManager;
    WifiManager* wifiManager;
    TimeSyncService* timeSyncService;
    HealthService* healthService;
    AuthContext* authContext;
};
