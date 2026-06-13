/**
 * @file app_bootstrap.cpp
 * @brief Implementation of AppBootstrap.
 */
#include "app_bootstrap.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_private/esp_psram_extram.h"
#include "nvs_flash.h"

#include "config_callbacks.hpp"
#include "esp_app_desc.h"
#include "screen_model.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static const char* kTag = "app_bootstrap";

static void reloadRuntimeConfigTrampoline(void* context) {
    if (context != nullptr) {
        static_cast<AppBootstrap*>(context)->reloadRuntimeConfig();
    }
}

bool AppBootstrap::initializeNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool AppBootstrap::initializePsram() {
#if CONFIG_SPIRAM && !CONFIG_SPIRAM_BOOT_INIT
    esp_err_t err = esp_psram_init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "PSRAM init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_psram_extram_add_to_heap_allocator();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "PSRAM heap add failed: %s", esp_err_to_name(err));
        return false;
    }
#if CONFIG_SPIRAM_USE_MALLOC
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
    ESP_LOGI(kTag, "PSRAM initialized after NVS");
#endif
    return true;
}

bool AppBootstrap::startAudioPipeline() {
    const config::AppConfig& activeConfig = m_configManager.active();

    snprintf(
        m_sessionId,
        sizeof(m_sessionId),
        "sess_%llu",
        static_cast<unsigned long long>(esp_timer_get_time() / 1000000ULL));

    if (!m_audioBoard.initialize()) {
        ESP_LOGW(kTag, "BOX-3 audio board init failed — capture/playback disabled");
        return true;
    }
    if (!m_audioBoard.openMicrophone(activeConfig.audio.sampleRateHz, activeConfig.audio.channels)) {
        ESP_LOGW(kTag, "BOX-3 mic open failed — capture disabled");
        return true;
    }

    uint32_t bootMicMaxAbs = 0;
    if (!m_audioBoard.probeMicrophonePcm(&bootMicMaxAbs)) {
        ESP_LOGW(kTag, "BOX-3 mic boot probe failed — capture may be silent");
    } else if (bootMicMaxAbs == 0) {
        ESP_LOGW(kTag, "BOX-3 mic boot probe silent (max_abs=0) — check hardware");
    }

    config::CallbacksConfig resolvedCallbacks = {};
    config::resolveCallbacks(activeConfig, resolvedCallbacks);
    m_audioUploadService.configure(
        resolvedCallbacks,
        activeConfig.audio,
        m_identity.deviceUid(),
        activeConfig.identity.deviceName,
        activeConfig.auth.token);
    m_utteranceStateMachine.configure(&m_audioUploadService, m_sessionId, activeConfig.vad);
    m_audioUploadService.setStateMachine(&m_utteranceStateMachine);

    m_vadService.configure(activeConfig.vad);
    m_audioCaptureService.configure(
        &m_audioBoard,
        &m_vadService,
        &m_utteranceStateMachine,
        &m_audioUploadService,
        activeConfig.audio,
        activeConfig.vad);
    m_audioPlaybackService.configure(
        &m_audioBoard, activeConfig.audio.sampleRateHz, activeConfig.audio.channels, &m_audioCaptureService);

    if (m_audioUploadService.start()) {
        ESP_LOGI(kTag, "upload service started");
    } else {
        ESP_LOGW(kTag, "upload service failed to start");
    }
    if (!m_audioPlaybackService.start()) {
        ESP_LOGW(kTag, "playback service failed to start");
    }
    if (!m_audioCaptureService.start()) {
        ESP_LOGW(kTag, "capture service failed to start");
    }

    if (m_audioUploadService.isConfigured()) {
        ESP_LOGI(
            kTag,
            "audio pipeline ready session=%s speech=%s capture=%d upload=%d",
            m_sessionId,
            resolvedCallbacks.speechUrl,
            m_audioCaptureService.isRunning() ? 1 : 0,
            m_audioUploadService.isRunning() ? 1 : 0);
    } else {
        ESP_LOGI(
            kTag,
            "audio pipeline ready (speech callbacks not configured) capture=%d",
            m_audioCaptureService.isRunning() ? 1 : 0);
    }
    return true;
}

bool AppBootstrap::startDisplayPipeline() {
    const config::AppConfig& activeConfig = m_configManager.active();
    m_displayService.setUiEventClient(&m_uiEventClient);
    m_displayService.configureLocalControls(&m_audioCaptureService, &m_audioPlaybackService);

    if (!m_displayService.initialize(activeConfig.display.defaultBrightness)) {
        ESP_LOGW(kTag, "display init failed — screen updates disabled");
        return true;
    }

    config::CallbacksConfig resolvedCallbacks = {};
    config::resolveCallbacks(activeConfig, resolvedCallbacks);
    m_uiEventClient.configure(
        resolvedCallbacks,
        m_identity.deviceUid(),
        activeConfig.identity.deviceName,
        activeConfig.auth.token);
    if (!m_uiEventClient.start()) {
        ESP_LOGW(kTag, "ui-event client failed to start");
    }

    ESP_LOGI(kTag, "display hardware ready (idle screen deferred until after CLI)");
    return true;
}

bool AppBootstrap::showDeferredIdleScreen() {
    if (!m_displayService.isReady()) {
        return false;
    }

    const config::AppConfig& activeConfig = m_configManager.active();
    if (!m_displayService.showIdleScreen(activeConfig.identity.deviceName)) {
        ESP_LOGW(kTag, "idle screen render failed");
        return false;
    }
    return true;
}

bool AppBootstrap::startHttpServer() {
    const config::AppConfig& activeConfig = m_configManager.active();
    m_authContext.configure(activeConfig.auth.token);

    m_apiContext.deviceUid = m_identity.deviceUid();
    m_apiContext.configManager = &m_configManager;
    m_apiContext.wifiManager = &m_wifiManager;
    m_apiContext.timeSyncService = &m_timeSyncService;
    m_apiContext.healthService = &m_healthService;
    m_apiContext.authContext = &m_authContext;
    m_apiContext.audioPlayback = &m_audioPlaybackService;
    m_apiContext.displayService = &m_displayService;
    m_apiContext.reloadRuntimeConfig = reloadRuntimeConfigTrampoline;
    m_apiContext.runtimeReloadContext = this;

    if (!m_httpServer.start(activeConfig.network.localHttpPort, &m_apiContext)) {
        ESP_LOGE(kTag, "HTTP server failed to start");
        return false;
    }
    return true;
}

bool AppBootstrap::startSerialCli() {
    m_cliContext.deviceUid = m_identity.deviceUid();
    m_cliContext.configManager = &m_configManager;
    m_cliContext.wifiManager = &m_wifiManager;
    m_cliContext.timeSyncService = &m_timeSyncService;
    m_cliContext.healthService = &m_healthService;
    m_cliContext.audioBoard = &m_audioBoard;
    m_cliContext.audioCapture = &m_audioCaptureService;
    m_cliContext.audioUpload = &m_audioUploadService;
    m_cliContext.vadService = &m_vadService;
    m_cliContext.utteranceFsm = &m_utteranceStateMachine;
    m_cliContext.displayService = &m_displayService;
    m_cliContext.reloadRuntimeConfig = reloadRuntimeConfigTrampoline;
    m_cliContext.runtimeReloadContext = this;

    if (!m_serialCli.start(&m_cliContext)) {
        ESP_LOGE(kTag, "Serial CLI failed to start");
        return false;
    }
    return true;
}

void AppBootstrap::reloadRuntimeConfig() {
    const config::AppConfig& activeConfig = m_configManager.active();

    m_authContext.configure(activeConfig.auth.token);
    m_vadService.configure(activeConfig.vad);
    m_utteranceStateMachine.configure(&m_audioUploadService, m_sessionId, activeConfig.vad);
    m_audioCaptureService.configure(
        &m_audioBoard,
        &m_vadService,
        &m_utteranceStateMachine,
        &m_audioUploadService,
        activeConfig.audio,
        activeConfig.vad);

    config::CallbacksConfig resolvedCallbacks = {};
    config::resolveCallbacks(activeConfig, resolvedCallbacks);
    m_audioUploadService.configure(
        resolvedCallbacks,
        activeConfig.audio,
        m_identity.deviceUid(),
        activeConfig.identity.deviceName,
        activeConfig.auth.token);
    m_uiEventClient.configure(
        resolvedCallbacks,
        m_identity.deviceUid(),
        activeConfig.identity.deviceName,
        activeConfig.auth.token);

    if (m_audioUploadService.isConfigured()) {
        ESP_LOGI(
            kTag,
            "runtime config reloaded vad=%u/%u speech=%s",
            static_cast<unsigned>(activeConfig.vad.speechStartThreshold),
            static_cast<unsigned>(activeConfig.vad.silenceFinalizeMs),
            resolvedCallbacks.speechUrl);
    } else {
        ESP_LOGI(
            kTag,
            "runtime config reloaded vad=%u/%u (speech callbacks not configured)",
            static_cast<unsigned>(activeConfig.vad.speechStartThreshold),
            static_cast<unsigned>(activeConfig.vad.silenceFinalizeMs));
    }
}

bool AppBootstrap::start() {
    if (!initializeNvs()) {
        return false;
    }

    if (!initializePsram()) {
        return false;
    }

    if (!m_identity.initialize()) {
        return false;
    }

    if (!m_configManager.initialize()) {
        return false;
    }

    // Display before Wi-Fi: BOX-3 LCD needs a DMA draw buffer; Wi-Fi init consumes
    // internal RAM and contends on the shared MSPI bus (esp-bsp display_audio_photo order).
    if (!startDisplayPipeline()) {
        ESP_LOGW(kTag, "display pipeline start failed");
    }

    if (!m_wifiManager.initialize()) {
        return false;
    }

    const config::AppConfig& activeConfig = m_configManager.active();
    if (activeConfig.wifi.ssid[0] != '\0') {
        m_wifiManager.applyConfig(activeConfig.wifi);
    }

    if (!m_timeSyncService.start(activeConfig.time)) {
        ESP_LOGW(kTag, "SNTP start failed");
    }

    // Serial CLI before HTTP/audio so the device stays recoverable if services fail.
    if (!startSerialCli()) {
        return false;
    }

    if (!startHttpServer()) {
        ESP_LOGW(kTag, "HTTP server failed to start — device remains usable via CLI");
    }

    if (!startAudioPipeline()) {
        ESP_LOGW(kTag, "audio pipeline start failed");
    }

    // Paint idle screen only after CLI/HTTP are up so a display bug cannot brick recovery.
    showDeferredIdleScreen();

    HealthInputs inputs = {};
    inputs.deviceUid = m_identity.deviceUid();
    inputs.deviceName = activeConfig.identity.deviceName;
    inputs.wifiState = m_wifiManager.stateLabel();
    inputs.rssi = m_wifiManager.rssi();
    inputs.timeTrusted = m_timeSyncService.isTimeTrusted();
    inputs.configDirty = m_configManager.isDirty();

    HealthSnapshot health = {};
    if (m_healthService.collect(inputs, health)) {
        ESP_LOGI(
            kTag,
            "ready uid=%s wifi=%s heap=%lu dirty=%d time_trusted=%d http_port=%u cli=%d",
            health.deviceUid,
            health.wifiState,
            static_cast<unsigned long>(health.freeHeap),
            health.configDirty,
            health.timeTrusted,
            static_cast<unsigned>(activeConfig.network.localHttpPort),
            static_cast<int>(m_serialCli.isRunning()));
    }

    return true;
}

const DeviceIdentity& AppBootstrap::identity() const {
    return m_identity;
}

const ConfigManager& AppBootstrap::configManager() const {
    return m_configManager;
}

const WifiManager& AppBootstrap::wifiManager() const {
    return m_wifiManager;
}

const TimeSyncService& AppBootstrap::timeSyncService() const {
    return m_timeSyncService;
}

const HealthService& AppBootstrap::healthService() const {
    return m_healthService;
}

const HttpServerService& AppBootstrap::httpServer() const {
    return m_httpServer;
}

const SerialCliService& AppBootstrap::serialCli() const {
    return m_serialCli;
}
