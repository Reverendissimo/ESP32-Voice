/**
 * @file cli_command_registry.cpp
 * @brief Implementation of CliCommandRegistry.
 */
#include "cli_command_registry.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "audio_capture_service.hpp"
#include "audio_types.hpp"
#include "audio_upload_service.hpp"
#include "box3_audio_board.hpp"
#include "display_service.hpp"
#include "screen_model.hpp"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config_callbacks.hpp"
#include "config_manager.hpp"
#include "utterance_state_machine.hpp"
#include "vad_service.hpp"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_system.h"
#include "health_service.hpp"
#include "time_sync_service.hpp"
#include "wifi_manager.hpp"

static CliContext* s_context = nullptr;

namespace {

constexpr const char* kApiVersion = "1";

const char* firmwareVersionLabel() {
    const esp_app_desc_t* appDesc = esp_app_get_description();
    if (appDesc == nullptr || appDesc->version[0] == '\0') {
        return "unknown";
    }
    return appDesc->version;
}

void printMaskedSecret(const char* label, const char* value) {
    if (value != nullptr && value[0] != '\0') {
        printf("%s: ***\n", label);
    } else {
        printf("%s: (empty)\n", label);
    }
}

void printConfig(const config::AppConfig& config, bool dirty) {
    printf("schema_version: %lu\n", static_cast<unsigned long>(config.schemaVersion));
    printf("device_name: %s\n", config.identity.deviceName);
    printMaskedSecret("auth_token", config.auth.token);
    printf("wifi_ssid: %s\n", config.wifi.ssid);
    printMaskedSecret("wifi_password", config.wifi.password);
    printf("http_port: %u\n", static_cast<unsigned>(config.network.localHttpPort));
    printf("callback_base_url: %s\n",
           config.network.callbackBaseUrl[0] != '\0' ? config.network.callbackBaseUrl : "(empty)");

    config::CallbacksConfig resolved = {};
    config::resolveCallbacks(config, resolved);
    printf("speech_url: %s\n", config.callbacks.speechUrl[0] != '\0' ? config.callbacks.speechUrl : "(empty)");
    printf("speech_url_effective: %s\n",
           resolved.speechUrl[0] != '\0' ? resolved.speechUrl : "(empty)");
    printf("speech_finalize_url: %s\n",
           config.callbacks.speechFinalizeUrl[0] != '\0' ? config.callbacks.speechFinalizeUrl : "(empty)");
    printf("speech_finalize_url_effective: %s\n",
           resolved.speechFinalizeUrl[0] != '\0' ? resolved.speechFinalizeUrl : "(empty)");
    printf("ui_event_url: %s\n",
           config.callbacks.uiEventUrl[0] != '\0' ? config.callbacks.uiEventUrl : "(empty)");
    printf("heartbeat_url: %s\n",
           config.callbacks.heartbeatUrl[0] != '\0' ? config.callbacks.heartbeatUrl : "(empty)");
    printf("vad_speech_threshold: %u\n", static_cast<unsigned>(config.vad.speechStartThreshold));
    printf("vad_silence_finalize_ms: %u\n", static_cast<unsigned>(config.vad.silenceFinalizeMs));
    printf("vad_pre_roll_padding_ms: %u\n", static_cast<unsigned>(config.vad.preRollPaddingMs));
    printf("vad_post_roll_padding_ms: %u\n", static_cast<unsigned>(config.vad.postRollPaddingMs));
    printf("timezone: %s\n", config.time.timezone);
    printf("sntp_server: %s\n", config.time.sntpServer);
    printf("dirty: %s\n", dirty ? "yes" : "no");
}

void notifyRuntimeConfigReload() {
    if (s_context != nullptr && s_context->reloadRuntimeConfig != nullptr) {
        s_context->reloadRuntimeConfig(s_context->runtimeReloadContext);
    }
}

bool applyPatchJson(cJSON* root, char* errorOut, size_t errorOutLen) {
    if (s_context == nullptr || s_context->configManager == nullptr || root == nullptr) {
        return false;
    }

    char* patchJson = cJSON_PrintUnformatted(root);
    if (patchJson == nullptr) {
        return false;
    }

    const bool ok = s_context->configManager->applyPatchJson(patchJson, errorOut, errorOutLen);
    cJSON_free(patchJson);
    if (ok) {
        notifyRuntimeConfigReload();
    }
    return ok;
}

bool applyWifiPatch(const char* ssid, const char* password, char* errorOut, size_t errorOutLen) {
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi == nullptr) {
        cJSON_Delete(root);
        return false;
    }

    cJSON_AddStringToObject(wifi, "ssid", ssid);
    cJSON_AddStringToObject(wifi, "password", password);

    const bool ok = applyPatchJson(root, errorOut, errorOutLen);
    cJSON_Delete(root);
    return ok;
}

int cmdVersion(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("firmware_version: %s\n", firmwareVersionLabel());
    printf("api_version: %s\n", kApiVersion);
    if (s_context != nullptr && s_context->deviceUid != nullptr) {
        printf("device_uid: %s\n", s_context->deviceUid);
    }
    return 0;
}

int cmdHelp(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("ESP32-Voice firmware %s (API v%s)\n\n", firmwareVersionLabel(), kApiVersion);
    printf(
        "Commands:\n"
        "  version\n"
        "  help\n"
        "  status\n"
        "  health\n"
        "  wifi_set <ssid> <password>\n"
        "  wifi_test\n"
        "  device_name_set <name>\n"
        "  auth_set <token>\n"
        "  callback_base_set <url>\n"
        "  callbacks_set <speech_url> <finalize_url>\n"
        "  ui_event_url_set <url>\n"
        "  heartbeat_url_set <url>\n"
        "  http_port_set <port>\n"
        "  vad_set <speech_threshold> <silence_ms>\n"
        "  vad_padding_set <pre_roll_ms> <post_roll_ms>\n"
        "  time_set <timezone> <sntp_server>\n"
        "  display_show <mode> [title] [subtitle]\n"
        "  audio_diag\n"
        "  upload_ping\n"
        "  upload_start\n"
        "  mic_probe\n"
        "  config_show\n"
        "  config_show_saved\n"
        "  config_load\n"
        "  config_save\n"
        "  config_revert\n"
        "  time_sync\n"
        "  reboot confirm\n"
        "  factory_reset confirm\n");
    return 0;
}

int cmdStatus(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr) {
        printf("CLI not ready\n");
        return 1;
    }

    printf("device_uid: %s\n", s_context->deviceUid != nullptr ? s_context->deviceUid : "");
    if (s_context->wifiManager != nullptr) {
        printf("wifi: %s rssi=%d\n", s_context->wifiManager->stateLabel(), s_context->wifiManager->rssi());
    }
    if (s_context->configManager != nullptr) {
        printf("config_dirty: %s\n", s_context->configManager->isDirty() ? "yes" : "no");
    }
    if (s_context->timeSyncService != nullptr) {
        printf("time_trusted: %s\n", s_context->timeSyncService->isTimeTrusted() ? "yes" : "no");
    }
    return 0;
}

int cmdHealth(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->healthService == nullptr) {
        printf("Health service unavailable\n");
        return 1;
    }

    HealthInputs inputs = {};
    inputs.deviceUid = s_context->deviceUid;
    if (s_context->configManager != nullptr) {
        inputs.deviceName = s_context->configManager->active().identity.deviceName;
        inputs.configDirty = s_context->configManager->isDirty();
    }
    if (s_context->wifiManager != nullptr) {
        inputs.wifiState = s_context->wifiManager->stateLabel();
        inputs.rssi = s_context->wifiManager->rssi();
    }
    if (s_context->timeSyncService != nullptr) {
        inputs.timeTrusted = s_context->timeSyncService->isTimeTrusted();
    }

    HealthSnapshot snapshot = {};
    if (!s_context->healthService->collect(inputs, snapshot)) {
        printf("Failed to collect health\n");
        return 1;
    }

    printf("device_uid: %s\n", snapshot.deviceUid);
    printf("device_name: %s\n", snapshot.deviceName);
    printf("firmware_version: %s\n", snapshot.firmwareVersion);
    printf("api_version: %s\n", snapshot.apiVersion);
    printf("main_state: %s\n", snapshot.mainState);
    printf("wifi_state: %s\n", snapshot.wifiState);
    printf("rssi: %d\n", snapshot.rssi);
    printf("uptime_ms: %llu\n", static_cast<unsigned long long>(snapshot.uptimeMs));
    printf("free_heap: %lu\n", static_cast<unsigned long>(snapshot.freeHeap));
    printf("battery_percent: %d\n", snapshot.batteryPercent);
    printf("time_trusted: %s\n", snapshot.timeTrusted ? "yes" : "no");
    printf("config_dirty: %s\n", snapshot.configDirty ? "yes" : "no");
    return 0;
}

int cmdWifiSet(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: wifi_set <ssid> <password>\n");
        return 1;
    }

    char error[128] = {};
    if (!applyWifiPatch(argv[1], argv[2], error, sizeof(error))) {
        printf("wifi_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }

    printf("Wi-Fi credentials updated in active config (not saved until config_save)\n");
    return 0;
}

int cmdDeviceNameSet(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: device_name_set <name>\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* identity = cJSON_AddObjectToObject(root, "identity");
    if (identity == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(identity, "deviceName", argv[1]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("device_name_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("device_name updated in active config\n");
    return 0;
}

int cmdAuthSet(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: auth_set <token>\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* auth = cJSON_AddObjectToObject(root, "auth");
    if (auth == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(auth, "token", argv[1]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("auth_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("auth token updated in active config\n");
    return 0;
}

int cmdHttpPortSet(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: http_port_set <port>\n");
        return 1;
    }

    const long port = strtol(argv[1], nullptr, 10);
    if (port <= 0 || port > 65535) {
        printf("http_port_set failed: port must be 1-65535\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* network = cJSON_AddObjectToObject(root, "network");
    if (network == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddNumberToObject(network, "localHttpPort", static_cast<double>(port));

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("http_port_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("http_port updated (reboot required for server to listen on new port)\n");
    return 0;
}

int cmdVadSet(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: vad_set <speech_threshold> <silence_ms>\n");
        printf("Example: vad_set 25 1200\n");
        return 1;
    }

    const long threshold = strtol(argv[1], nullptr, 10);
    const long silenceMs = strtol(argv[2], nullptr, 10);
    if (threshold <= 0 || threshold > 1000) {
        printf("vad_set failed: speech_threshold must be 1-1000\n");
        return 1;
    }
    if (silenceMs < 200 || silenceMs > 10000) {
        printf("vad_set failed: silence_ms must be 200-10000\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* vad = cJSON_AddObjectToObject(root, "vad");
    if (vad == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddNumberToObject(vad, "speechStartThreshold", static_cast<double>(threshold));
    cJSON_AddNumberToObject(vad, "silenceFinalizeMs", static_cast<double>(silenceMs));

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("vad_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("VAD thresholds updated (active immediately)\n");
    return 0;
}

int cmdVadPaddingSet(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: vad_padding_set <pre_roll_ms> <post_roll_ms>\n");
        printf("Example: vad_padding_set 1000 1000\n");
        printf("Set to 0 to disable pre-roll or post-roll padding\n");
        return 1;
    }

    const long preRollMs = strtol(argv[1], nullptr, 10);
    const long postRollMs = strtol(argv[2], nullptr, 10);
    if (preRollMs < 0 || preRollMs > static_cast<long>(config::kMaxUtterancePaddingMs)) {
        printf(
            "vad_padding_set failed: pre_roll_ms must be 0-%u\n",
            static_cast<unsigned>(config::kMaxUtterancePaddingMs));
        return 1;
    }
    if (postRollMs < 0 || postRollMs > static_cast<long>(config::kMaxUtterancePaddingMs)) {
        printf(
            "vad_padding_set failed: post_roll_ms must be 0-%u\n",
            static_cast<unsigned>(config::kMaxUtterancePaddingMs));
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* vad = cJSON_AddObjectToObject(root, "vad");
    if (vad == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddNumberToObject(vad, "preRollPaddingMs", static_cast<double>(preRollMs));
    cJSON_AddNumberToObject(vad, "postRollPaddingMs", static_cast<double>(postRollMs));

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("vad_padding_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("VAD padding updated (active immediately)\n");
    return 0;
}

int cmdUiEventUrlSet(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: ui_event_url_set <url>\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* callbacks = cJSON_AddObjectToObject(root, "callbacks");
    if (callbacks == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(callbacks, "uiEventUrl", argv[1]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("ui_event_url_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("ui_event_url updated in active config\n");
    return 0;
}

int cmdHeartbeatUrlSet(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: heartbeat_url_set <url>\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* callbacks = cJSON_AddObjectToObject(root, "callbacks");
    if (callbacks == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(callbacks, "heartbeatUrl", argv[1]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("heartbeat_url_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("heartbeat_url updated in active config\n");
    return 0;
}

int cmdCallbackBaseSet(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: callback_base_set <url>\n");
        printf("Example: callback_base_set http://192.168.1.10:8080/api/v1\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* network = cJSON_AddObjectToObject(root, "network");
    if (network == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(network, "callbackBaseUrl", argv[1]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("callback_base_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);

    printf("callback_base_url updated (derives /speech/stream and /speech/finalize if not set explicitly)\n");
    printf("Run config_save to persist, then speak to test upload\n");
    return 0;
}

int cmdCallbacksSet(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: callbacks_set <speech_url> <finalize_url>\n");
        printf("Example: callbacks_set http://192.168.1.10:8080/speech/stream http://192.168.1.10:8080/speech/finalize\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* callbacks = cJSON_AddObjectToObject(root, "callbacks");
    if (callbacks == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(callbacks, "speechUrl", argv[1]);
    cJSON_AddStringToObject(callbacks, "speechFinalizeUrl", argv[2]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("callbacks_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);

    printf("Speech callback URLs updated in active config\n");
    printf("Run config_save to persist across reboot\n");
    return 0;
}

int cmdWifiTest(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr || s_context->wifiManager == nullptr) {
        printf("Wi-Fi services unavailable\n");
        return 1;
    }

    const config::WifiConfig& wifi = s_context->configManager->active().wifi;
    if (wifi.ssid[0] == '\0') {
        printf("No SSID in active config\n");
        return 1;
    }

    const bool ok = s_context->wifiManager->testCredentials(wifi.ssid, wifi.password, 15000);
    printf("wifi_test: %s\n", ok ? "connected" : "failed");
    return ok ? 0 : 1;
}

int cmdTimeSet(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: time_set <timezone> <sntp_server>\n");
        printf("Example: time_set UTC0 pool.ntp.org\n");
        return 1;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* time = cJSON_AddObjectToObject(root, "time");
    if (time == nullptr) {
        cJSON_Delete(root);
        return 1;
    }
    cJSON_AddStringToObject(time, "timezone", argv[1]);
    cJSON_AddStringToObject(time, "sntpServer", argv[2]);

    char error[128] = {};
    if (!applyPatchJson(root, error, sizeof(error))) {
        cJSON_Delete(root);
        printf("time_set failed: %s\n", error[0] != '\0' ? error : "unknown error");
        return 1;
    }
    cJSON_Delete(root);
    printf("time settings updated in active config\n");
    return 0;
}

int cmdDisplayShow(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: display_show <mode> [title] [subtitle]\n");
        printf("Modes: idle listening thinking message choice_list confirm notification form\n");
        return 1;
    }
    if (s_context == nullptr || s_context->displayService == nullptr || !s_context->displayService->isReady()) {
        printf("Display not ready\n");
        return 1;
    }

    display::ScreenMode mode = display::ScreenMode::Unknown;
    if (!display::screenModeFromString(argv[1], mode)) {
        printf("display_show failed: unknown mode '%s'\n", argv[1]);
        return 1;
    }

    const char* title = (argc >= 3) ? argv[2] : "";
    const char* subtitle = (argc >= 4) ? argv[3] : "";

    if (!s_context->displayService->showSimpleScreen(mode, title, subtitle)) {
        printf("display_show failed\n");
        return 1;
    }

    printf("display updated mode=%s\n", display::screenModeToString(mode));
    return 0;
}

int cmdMicProbe(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->audioBoard == nullptr || !s_context->audioBoard->isMicrophoneOpen()) {
        printf("Microphone not open\n");
        return 1;
    }

    if (s_context->audioCapture != nullptr && s_context->audioCapture->isRunning()) {
        printf("mic_probe: capture is running — use audio_diag (cannot pause reader safely)\n");
        return 1;
    }

    esp_codec_dev_handle_t mic = s_context->audioBoard->microphone();
    const uint8_t hwChannels = s_context->audioBoard->micChannels();
    const size_t probeBytes = audio::kBytesPerFrame * ((hwChannels > 1) ? hwChannels : 1);
    auto* buffer = static_cast<int16_t*>(
        heap_caps_malloc(probeBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        printf("mic_probe: out of memory\n");
        return 1;
    }

    printf("Speak now for 2 seconds...\n");
    uint32_t maxAbs = 0;
    uint32_t okReads = 0;
    for (int i = 0; i < 100; ++i) {
        const int readRc = esp_codec_dev_read(mic, buffer, static_cast<int>(probeBytes));
        if (readRc == ESP_CODEC_DEV_OK) {
            ++okReads;
            const size_t sampleCount = probeBytes / sizeof(int16_t);
            for (size_t s = 0; s < sampleCount; ++s) {
                const int32_t sample = buffer[s];
                const uint32_t absSample = static_cast<uint32_t>(sample >= 0 ? sample : -sample);
                if (absSample > maxAbs) {
                    maxAbs = absSample;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    heap_caps_free(buffer);

    printf("mic_probe: ok_reads=%" PRIu32 " max_abs=%" PRIu32 "\n", okReads, maxAbs);
    if (maxAbs == 0) {
        printf("mic_probe: no PCM detected — hardware/driver path still silent\n");
        return 1;
    }
    printf("mic_probe: microphone is delivering audio\n");
    return 0;
}

int cmdUploadStart(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->audioUpload == nullptr) {
        printf("upload_start: upload service unavailable\n");
        return 1;
    }
    if (s_context->audioUpload->isRunning()) {
        printf("upload_start: already running\n");
        return 0;
    }
    if (!s_context->audioUpload->start()) {
        printf("upload_start: failed (see serial log)\n");
        return 1;
    }
    printf("upload_start: ok\n");
    return 0;
}

int cmdUploadPing(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->audioUpload == nullptr) {
        printf("upload_ping: upload service unavailable\n");
        return 1;
    }
    if (!s_context->audioUpload->isConfigured()) {
        printf("upload_ping: speech callbacks not configured\n");
        return 1;
    }
    if (!s_context->audioUpload->pingServer()) {
        printf("upload_ping: failed (see serial log)\n");
        return 1;
    }
    printf("upload_ping: ok\n");
    return 0;
}

int cmdAudioDiag(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }

    const config::AppConfig& active = s_context->configManager->active();
    config::CallbacksConfig resolved = {};
    config::resolveCallbacks(active, resolved);

    printf("audio_board_init: %s\n",
           s_context->audioBoard != nullptr && s_context->audioBoard->isInitialized() ? "yes" : "no");
    printf("mic_open: %s\n",
           s_context->audioBoard != nullptr && s_context->audioBoard->isMicrophoneOpen() ? "yes" : "no");
    if (s_context->audioBoard != nullptr) {
        printf("mic_hw_channels: %u\n", static_cast<unsigned>(s_context->audioBoard->micChannels()));
        printf("speaker_open: %s\n", s_context->audioBoard->isSpeakerOpen() ? "yes" : "no");
    }
    printf("capture_running: %s\n",
           s_context->audioCapture != nullptr && s_context->audioCapture->isRunning() ? "yes" : "no");
    printf("upload_configured: %s\n",
           s_context->audioUpload != nullptr && s_context->audioUpload->isConfigured() ? "yes" : "no");
    if (s_context->audioUpload != nullptr) {
        printf("upload_running: %s\n", s_context->audioUpload->isRunning() ? "yes" : "no");
        printf("upload_posts_ok: %" PRIu32 "\n", s_context->audioUpload->postsOkCount());
        printf("upload_posts_fail: %" PRIu32 "\n", s_context->audioUpload->postsFailCount());
        printf("upload_chunks_queued: %" PRIu32 "\n", s_context->audioUpload->chunksQueuedCount());
        printf("upload_chunks_dropped: %" PRIu32 "\n", s_context->audioUpload->chunksDroppedCount());
    }
    printf("speech_url_effective: %s\n",
           resolved.speechUrl[0] != '\0' ? resolved.speechUrl : "(empty)");
    printf("speech_finalize_url_effective: %s\n",
           resolved.speechFinalizeUrl[0] != '\0' ? resolved.speechFinalizeUrl : "(empty)");

    if (s_context->vadService != nullptr) {
        const uint32_t energyThreshold =
            static_cast<uint32_t>(active.vad.speechStartThreshold) * 40U;
        printf(
            "vad_threshold: %u (energy >= %" PRIu32 ")\n",
            static_cast<unsigned>(active.vad.speechStartThreshold),
            energyThreshold);
        printf("vad_in_speech: %s\n", s_context->vadService->isInSpeech() ? "yes" : "no");
        printf(
            "vad_pre_roll_padding_ms: %u\n",
            static_cast<unsigned>(active.vad.preRollPaddingMs));
        printf(
            "vad_post_roll_padding_ms: %u\n",
            static_cast<unsigned>(active.vad.postRollPaddingMs));
    }

    if (s_context->audioCapture != nullptr) {
        printf("mic_frames_ok: %" PRIu32 "\n", s_context->audioCapture->framesOkCount());
        printf("mic_last_energy: %" PRIu32 "\n", s_context->audioCapture->lastFrameEnergy());
        printf("mic_peak_energy: %" PRIu32 "\n", s_context->audioCapture->peakFrameEnergy());
        printf("mic_max_sample_abs: %" PRIu32 "\n", s_context->audioCapture->maxSampleAbs());
        printf("mic_read_failures: %" PRIu32 "\n", s_context->audioCapture->readFailCount());
    }

    if (s_context->utteranceFsm != nullptr) {
        printf("utterance_streaming: %s\n", s_context->utteranceFsm->isStreaming() ? "yes" : "no");
        if (s_context->utteranceFsm->isStreaming()) {
            printf("utterance_id: %s\n", s_context->utteranceFsm->utteranceId());
        }
    }

    printf("Tip: shout at the BOX, run audio_diag again — mic_last_energy should jump above vad threshold\n");
    return 0;
}

int cmdConfigShow(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    printf("[active config]\n");
    printConfig(s_context->configManager->active(), s_context->configManager->isDirty());
    return 0;
}

int cmdConfigShowSaved(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    printf("[saved config]\n");
    printConfig(s_context->configManager->saved(), false);
    return 0;
}

int cmdConfigLoad(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    if (!s_context->configManager->loadSavedIntoActive()) {
        printf("config_load failed: no saved config\n");
        return 1;
    }
    notifyRuntimeConfigReload();
    printf("Loaded saved config into active RAM\n");
    return 0;
}

int cmdConfigSave(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    if (!s_context->configManager->saveActive()) {
        printf("config_save failed\n");
        return 1;
    }
    notifyRuntimeConfigReload();
    printf("Active config saved to NVS\n");
    return 0;
}

int cmdConfigRevert(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->configManager == nullptr) {
        return 1;
    }
    s_context->configManager->revertActive();
    notifyRuntimeConfigReload();
    printf("Reverted active config to saved/default baseline\n");
    return 0;
}

int cmdTimeSync(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (s_context == nullptr || s_context->timeSyncService == nullptr) {
        return 1;
    }
    const bool ok = s_context->timeSyncService->syncNow();
    printf("time_sync: %s (trusted=%s)\n", ok ? "ok" : "failed", s_context->timeSyncService->isTimeTrusted() ? "yes" : "no");
    return ok ? 0 : 1;
}

int cmdReboot(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "confirm") != 0) {
        printf("This will reboot the device. Run: reboot confirm\n");
        return 1;
    }
    printf("Rebooting...\n");
    esp_restart();
    return 0;
}

int cmdFactoryReset(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "confirm") != 0) {
        printf("This will erase saved config. Run: factory_reset confirm\n");
        return 1;
    }
    if (s_context != nullptr && s_context->configManager != nullptr) {
        s_context->configManager->resetSaved();
    }
    printf("Factory reset complete. Rebooting...\n");
    esp_restart();
    return 0;
}

bool registerCommand(const char* name, const char* help, esp_console_cmd_func_t func) {
    esp_console_cmd_t cmd = {};
    cmd.command = name;
    cmd.help = help;
    cmd.func = func;
    return esp_console_cmd_register(&cmd) == ESP_OK;
}

}  // namespace

bool CliCommandRegistry::registerCommands(const CliContext* context) const {
    if (context == nullptr) {
        return false;
    }

    s_context = const_cast<CliContext*>(context);

    return registerCommand("version", "Show firmware and API version", cmdVersion) &&
           registerCommand("help", "Show available commands", cmdHelp) &&
           registerCommand("status", "Show brief device status", cmdStatus) &&
           registerCommand("health", "Show health snapshot", cmdHealth) &&
           registerCommand("wifi_set", "wifi_set <ssid> <password>", cmdWifiSet) &&
           registerCommand("wifi_test", "Test active Wi-Fi credentials", cmdWifiTest) &&
           registerCommand("device_name_set", "device_name_set <name>", cmdDeviceNameSet) &&
           registerCommand("auth_set", "auth_set <token>", cmdAuthSet) &&
           registerCommand("callback_base_set", "callback_base_set <url>", cmdCallbackBaseSet) &&
           registerCommand("callbacks_set", "callbacks_set <speech_url> <finalize_url>", cmdCallbacksSet) &&
           registerCommand("ui_event_url_set", "ui_event_url_set <url>", cmdUiEventUrlSet) &&
           registerCommand("heartbeat_url_set", "heartbeat_url_set <url>", cmdHeartbeatUrlSet) &&
           registerCommand("http_port_set", "http_port_set <port>", cmdHttpPortSet) &&
           registerCommand("vad_set", "vad_set <speech_threshold> <silence_ms>", cmdVadSet) &&
           registerCommand(
               "vad_padding_set",
               "vad_padding_set <pre_roll_ms> <post_roll_ms>",
               cmdVadPaddingSet) &&
           registerCommand("time_set", "time_set <timezone> <sntp_server>", cmdTimeSet) &&
           registerCommand("display_show", "display_show <mode> [title] [subtitle]", cmdDisplayShow) &&
           registerCommand("audio_diag", "Show audio capture/upload diagnostics", cmdAudioDiag) &&
           registerCommand("upload_ping", "POST test speech stream to callback server", cmdUploadPing) &&
           registerCommand("upload_start", "Start/retry upload worker task", cmdUploadStart) &&
           registerCommand("mic_probe", "Direct mic read test (speak during probe)", cmdMicProbe) &&
           registerCommand("config_show", "Show active config (masked)", cmdConfigShow) &&
           registerCommand("config_show_saved", "Show saved config (masked)", cmdConfigShowSaved) &&
           registerCommand("config_load", "Load saved config into active RAM", cmdConfigLoad) &&
           registerCommand("config_save", "Persist active config to NVS", cmdConfigSave) &&
           registerCommand("config_revert", "Revert active config changes", cmdConfigRevert) &&
           registerCommand("time_sync", "Trigger SNTP sync", cmdTimeSync) &&
           registerCommand("reboot", "Reboot device (use: reboot confirm)", cmdReboot) &&
           registerCommand("factory_reset", "Erase saved config (use: factory_reset confirm)", cmdFactoryReset);
}
