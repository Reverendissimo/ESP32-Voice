/**
 * @file health_service.hpp
 * @brief Builds health and status snapshots.
 *
 * Responsibilities:
 * - uptime, heap, wifi, dirty flag, time trust
 *
 * Non-responsibilities:
 * - HTTP server lifecycle
 * - sensor drivers
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct HealthInputs {
    const char* deviceUid;
    const char* deviceName;
    const char* wifiState;
    int8_t rssi;
    bool timeTrusted;
    bool configDirty;
};

struct HealthSnapshot {
    char deviceUid[32];
    char deviceName[32];
    char firmwareVersion[32];
    char apiVersion[16];
    char mainState[16];
    char wifiState[16];
    uint64_t uptimeMs;
    uint32_t freeHeap;
    int8_t rssi;
    int8_t batteryPercent;
    bool timeTrusted;
    bool configDirty;
};

/**
 * @brief Collects device health snapshots for diagnostics endpoints.
 */
class HealthService {
public:
    /**
     * @brief Collects a health snapshot into outSnapshot.
     */
    bool collect(const HealthInputs& inputs, HealthSnapshot& outSnapshot) const;
};
