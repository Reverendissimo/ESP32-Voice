/**
 * @file wifi_manager.hpp
 * @brief Owns Wi-Fi station connection lifecycle.
 *
 * Responsibilities:
 * - connect, disconnect, expose Wi-Fi state
 *
 * Non-responsibilities:
 * - config persistence
 * - HTTP API handling
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_models.hpp"

/**
 * @brief Wi-Fi station manager.
 */
class WifiManager {
public:
    /**
     * @brief Initializes network stack and starts station mode.
     */
    bool initialize();

    /**
     * @brief Applies Wi-Fi credentials and attempts connection.
     */
    bool applyConfig(const config::WifiConfig& wifiConfig);

    /**
     * @brief Tests credentials without persisting them.
     */
    bool testCredentials(const char* ssid, const char* password, uint32_t timeoutMs);

    /**
     * @brief Returns true when connected to an AP.
     */
    bool isConnected() const;

    /**
     * @brief Returns current RSSI when connected, otherwise 0.
     */
    int8_t rssi() const;

    /**
     * @brief Returns human-readable Wi-Fi state label.
     */
    const char* stateLabel() const;

private:
    bool m_initialized = false;
    bool m_connected = false;
    int8_t m_rssi = 0;
};
