/**
 * @file app_bootstrap.hpp
 * @brief Owns firmware startup and subsystem wiring order.
 */
#pragma once

#include <stdbool.h>

#include "api_context.hpp"
#include "auth_context.hpp"
#include "cli_context.hpp"
#include "config_manager.hpp"
#include "device_identity.hpp"
#include "health_service.hpp"
#include "http_server_service.hpp"
#include "serial_cli_service.hpp"
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
    const HttpServerService& httpServer() const;
    const SerialCliService& serialCli() const;

private:
    bool initializeNvs();
    bool startHttpServer();
    bool startSerialCli();

    DeviceIdentity m_identity;
    ConfigManager m_configManager;
    WifiManager m_wifiManager;
    TimeSyncService m_timeSyncService;
    HealthService m_healthService;
    AuthContext m_authContext;
    HttpServerService m_httpServer;
    SerialCliService m_serialCli;
    ApiContext m_apiContext = {};
    CliContext m_cliContext = {};
};
