/**
 * @file app_bootstrap.hpp
 * @brief Owns firmware startup and subsystem wiring order.
 */
#pragma once

#include <stdbool.h>

#include "api_context.hpp"
#include "audio_capture_service.hpp"
#include "audio_playback_service.hpp"
#include "audio_upload_service.hpp"
#include "auth_context.hpp"
#include "box3_audio_board.hpp"
#include "cli_context.hpp"
#include "config_manager.hpp"
#include "display_service.hpp"
#include "device_identity.hpp"
#include "health_service.hpp"
#include "http_server_service.hpp"
#include "ota_service.hpp"
#include "serial_cli_service.hpp"
#include "time_sync_service.hpp"
#include "ui_event_client.hpp"
#include "utterance_state_machine.hpp"
#include "vad_service.hpp"
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

    /**
     * @brief Re-applies runtime services from active config (after patch/load).
     */
    void reloadRuntimeConfig();

    const DeviceIdentity& identity() const;
    const ConfigManager& configManager() const;
    const WifiManager& wifiManager() const;
    const TimeSyncService& timeSyncService() const;
    const HealthService& healthService() const;
    const HttpServerService& httpServer() const;
    const SerialCliService& serialCli() const;

private:
    bool initializeNvs();
    bool initializePsram();
    bool startAudioPipeline();
    bool startDisplayPipeline();
    bool showDeferredIdleScreen();
    bool startHttpServer();
    bool startSerialCli();

    DeviceIdentity m_identity;
    ConfigManager m_configManager;
    WifiManager m_wifiManager;
    TimeSyncService m_timeSyncService;
    HealthService m_healthService;
    AuthContext m_authContext;
    OtaService m_otaService;
    HttpServerService m_httpServer;
    SerialCliService m_serialCli;
    DisplayService m_displayService;
    UiEventClient m_uiEventClient;
    Box3AudioBoard m_audioBoard;
    VadService m_vadService;
    AudioUploadService m_audioUploadService;
    UtteranceStateMachine m_utteranceStateMachine;
    AudioCaptureService m_audioCaptureService;
    AudioPlaybackService m_audioPlaybackService;
    char m_sessionId[48];
    ApiContext m_apiContext = {};
    CliContext m_cliContext = {};
};
