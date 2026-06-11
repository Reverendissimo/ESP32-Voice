/**
 * @file config_models.hpp
 * @brief Defines typed configuration schema structures.
 *
 * Responsibilities:
 * - central config field definitions
 *
 * Non-responsibilities:
 * - validation logic
 * - NVS persistence
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace config {

constexpr uint32_t kSchemaVersion = 1;

struct IdentityConfig {
    char deviceName[32];
};

struct AuthConfig {
    char token[64];
};

struct WifiConfig {
    char ssid[32];
    char password[64];
};

struct NetworkConfig {
    char callbackBaseUrl[128];
    uint16_t localHttpPort;
};

struct CallbacksConfig {
    char speechUrl[128];
    char speechFinalizeUrl[128];
    char uiEventUrl[128];
    char heartbeatUrl[128];
};

struct PresenceConfig {
    bool radarEnabled;
};

struct VadConfig {
    uint16_t speechStartThreshold;
    uint16_t silenceFinalizeMs;
};

struct AudioConfig {
    uint16_t sampleRateHz;
    uint8_t channels;
};

struct DisplayConfig {
    uint8_t defaultBrightness;
};

struct IrConfig {
    uint16_t learnTimeoutMs;
};

struct BatteryAlarmConfig {
    bool enabled;
    uint8_t lowTriggerPercent;
    uint8_t lowClearPercent;
    uint32_t minDurationMs;
    uint32_t cooldownMs;
};

struct EnvironmentAlarmConfig {
    bool highEnabled;
    float highTriggerC;
    float highClearC;
    bool lowEnabled;
    float lowTriggerC;
    float lowClearC;
    uint32_t minDurationMs;
    uint32_t cooldownMs;
};

struct TimeConfig {
    char timezone[32];
    char sntpServer[64];
    uint32_t syncIntervalSec;
};

struct DiagnosticsConfig {
    uint16_t recentLogCapacity;
};

struct AppConfig {
    uint32_t schemaVersion;
    IdentityConfig identity;
    AuthConfig auth;
    WifiConfig wifi;
    NetworkConfig network;
    CallbacksConfig callbacks;
    PresenceConfig presence;
    VadConfig vad;
    AudioConfig audio;
    DisplayConfig display;
    IrConfig ir;
    BatteryAlarmConfig batteryAlarm;
    EnvironmentAlarmConfig environmentAlarm;
    TimeConfig time;
    DiagnosticsConfig diagnostics;
};

/**
 * @brief Returns factory default configuration values.
 */
AppConfig makeDefaults();

}  // namespace config
