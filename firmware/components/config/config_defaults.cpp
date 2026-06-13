/**
 * @file config_defaults.cpp
 * @brief Factory default configuration values.
 */
#include "config_models.hpp"

#include <string.h>

namespace config {

AppConfig makeDefaults() {
    AppConfig defaults = {};
    defaults.schemaVersion = kSchemaVersion;

    strncpy(defaults.identity.deviceName, "esp32-voice", sizeof(defaults.identity.deviceName) - 1);

    defaults.network.localHttpPort = 80;

    defaults.presence.radarEnabled = true;

    defaults.vad.speechStartThreshold = 25;
    defaults.vad.silenceFinalizeMs = 1200;

    defaults.audio.sampleRateHz = 16000;
    defaults.audio.channels = 1;

    defaults.display.defaultBrightness = 70;

    defaults.ir.learnTimeoutMs = 15000;

    defaults.batteryAlarm.enabled = true;
    defaults.batteryAlarm.lowTriggerPercent = 15;
    defaults.batteryAlarm.lowClearPercent = 20;
    defaults.batteryAlarm.minDurationMs = 5000;
    defaults.batteryAlarm.cooldownMs = 60000;

    defaults.environmentAlarm.highEnabled = true;
    defaults.environmentAlarm.highTriggerC = 35.0f;
    defaults.environmentAlarm.highClearC = 32.0f;
    defaults.environmentAlarm.lowEnabled = false;
    defaults.environmentAlarm.lowTriggerC = 5.0f;
    defaults.environmentAlarm.lowClearC = 8.0f;
    defaults.environmentAlarm.minDurationMs = 10000;
    defaults.environmentAlarm.cooldownMs = 60000;

    strncpy(defaults.time.timezone, "UTC0", sizeof(defaults.time.timezone) - 1);
    strncpy(defaults.time.sntpServer, "pool.ntp.org", sizeof(defaults.time.sntpServer) - 1);
    defaults.time.syncIntervalSec = 3600;

    defaults.diagnostics.recentLogCapacity = 100;

    return defaults;
}

}  // namespace config
