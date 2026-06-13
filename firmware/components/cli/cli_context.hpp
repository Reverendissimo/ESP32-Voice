/**
 * @file cli_context.hpp
 * @brief Shared service pointers for serial CLI command handlers.
 */
#pragma once

class AudioCaptureService;
class AudioUploadService;
class Box3AudioBoard;
class ConfigManager;
class DisplayService;
class HealthService;
class TimeSyncService;
class UtteranceStateMachine;
class VadService;
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
    Box3AudioBoard* audioBoard;
    AudioCaptureService* audioCapture;
    AudioUploadService* audioUpload;
    VadService* vadService;
    UtteranceStateMachine* utteranceFsm;
    DisplayService* displayService;
    void (*reloadRuntimeConfig)(void* context);
    void* runtimeReloadContext;
};
