/**
 * @file app_bootstrap.hpp
 * @brief Owns firmware startup and subsystem wiring order.
 *
 * Responsibilities:
 * - initialize NVS and core services
 * - start configured subsystems
 *
 * Non-responsibilities:
 * - HTTP route business logic
 * - hardware driver details
 */
#pragma once

#include <stdbool.h>

#include "config_manager.hpp"
#include "device_identity.hpp"
#include "health_service.hpp"
#include "time_sync_service.hpp"
#include "wifi_manager.hpp"

/**
 * @brief Application bootstrap and subsystem wiring.
 */
class AppBootstrap {
public:
    /**
     * @brief Starts platform services in dependency order.
     */
    bool start();

    const DeviceIdentity& identity() const;
    const ConfigManager& configManager() const;
    const WifiManager& wifiManager() const;
    const TimeSyncService& timeSyncService() const;
    const HealthService& healthService() const;

private:
    bool initializeNvs();

    DeviceIdentity m_identity;
    ConfigManager m_configManager;
    WifiManager m_wifiManager;
    TimeSyncService m_timeSyncService;
    HealthService m_healthService;
};
