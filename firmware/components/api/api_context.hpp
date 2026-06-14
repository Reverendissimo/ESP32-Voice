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

class AudioPlaybackService;
class AuthContext;
class DisplayService;
class ConfigManager;
class HealthService;
class OtaService;
class TimeSyncService;
class UtteranceStateMachine;
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
    OtaService* otaService;
    UtteranceStateMachine* utteranceFsm;
    AudioPlaybackService* audioPlayback;
    DisplayService* displayService;
    void (*reloadRuntimeConfig)(void* context);
    void* runtimeReloadContext;
};
