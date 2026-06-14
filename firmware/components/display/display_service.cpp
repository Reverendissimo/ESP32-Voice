/**
 * @file display_service.cpp
 * @brief Implementation of DisplayService.
 */
#include "display_service.hpp"

#include <stdio.h>
#include <string.h>

#include "audio_capture_service.hpp"
#include "audio_playback_service.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "ui_event_client.hpp"

static const char* kTag = "display_service";

namespace {

constexpr const char* kLocalActionPrefix = "local:";

bool isLocalAction(const char* actionId) {
    return actionId != nullptr && strncmp(actionId, kLocalActionPrefix, strlen(kLocalActionPrefix)) == 0;
}

const char* localActionName(const char* actionId) {
    if (!isLocalAction(actionId)) {
        return actionId;
    }
    return actionId + strlen(kLocalActionPrefix);
}

const char* subtitleForMode(display::ScreenMode mode) {
    switch (mode) {
        case display::ScreenMode::Recording:
            return "Recording";
        case display::ScreenMode::Speaking:
            return "Speaking";
        case display::ScreenMode::MicOff:
            return "MICROPHONE OFF";
        case display::ScreenMode::Listening:
            return "Listening";
        case display::ScreenMode::Idle:
        default:
            return "Listening";
    }
}

}  // namespace

void DisplayService::setUiEventClient(UiEventClient* uiEventClient) {
    m_uiEventClient = uiEventClient;
}

void DisplayService::configureLocalControls(AudioCaptureService* capture, AudioPlaybackService* playback) {
    m_capture = capture;
    m_playback = playback;
}

bool DisplayService::initialize(uint8_t brightnessPercent) {
    if (m_screenBuffer == nullptr) {
        m_screenBuffer = display::allocScreenModelPsram();
        if (m_screenBuffer == nullptr) {
            ESP_LOGE(kTag, "screen buffer PSRAM alloc failed");
            return false;
        }
    }

    m_renderer.setButtonHandler(onButtonPressed, this);
    m_renderer.setSliderHandler(onSliderChanged, this);
    return m_renderer.initialize(brightnessPercent);
}

bool DisplayService::isReady() const {
    return m_renderer.isReady();
}

bool DisplayService::showScreen(const display::ScreenModel& model) {
    if (!m_renderer.scheduleRender(model)) {
        ESP_LOGW(kTag, "render failed");
        return false;
    }
    return true;
}

bool DisplayService::buildMainScreenModel(display::ScreenMode mode, const char* subtitle, bool subtitleAlert) {
    if (m_screenBuffer == nullptr) {
        return false;
    }

    display::resetScreenModel(*m_screenBuffer);
    m_screenBuffer->mode = mode;
    m_currentMode = mode;
    m_screenBuffer->subtitleAlert = subtitleAlert;

    if (m_idleTitle[0] != '\0') {
        strncpy(m_screenBuffer->title, m_idleTitle, sizeof(m_screenBuffer->title) - 1);
    } else {
        strncpy(m_screenBuffer->title, "ESP32 Voice", sizeof(m_screenBuffer->title) - 1);
    }
    m_screenBuffer->title[sizeof(m_screenBuffer->title) - 1] = '\0';

    if (subtitle != nullptr) {
        strncpy(m_screenBuffer->subtitle, subtitle, sizeof(m_screenBuffer->subtitle) - 1);
        m_screenBuffer->subtitle[sizeof(m_screenBuffer->subtitle) - 1] = '\0';
    }

    const bool micMuted = m_capture != nullptr && m_capture->isUserMuted();

    uint8_t idx = 0;

    display::ScreenComponent& volumeLabel = m_screenBuffer->components[idx++];
    volumeLabel.kind = display::ComponentKind::Text;
    strncpy(volumeLabel.id, "volume_label", sizeof(volumeLabel.id) - 1);
    snprintf(
        volumeLabel.text,
        sizeof(volumeLabel.text),
        "Playback volume: %u%%",
        static_cast<unsigned>(m_playback != nullptr ? m_playback->volumePercent() : 80U));

    display::ScreenComponent& volumeSlider = m_screenBuffer->components[idx++];
    volumeSlider.kind = display::ComponentKind::Slider;
    strncpy(volumeSlider.id, "volume", sizeof(volumeSlider.id) - 1);
    strncpy(volumeSlider.text, "Volume", sizeof(volumeSlider.text) - 1);
    strncpy(volumeSlider.actionId, "local:volume", sizeof(volumeSlider.actionId) - 1);
    volumeSlider.progressPercent = m_playback != nullptr ? m_playback->volumePercent() : 80;

    display::ScreenComponent& micButton = m_screenBuffer->components[idx++];
    micButton.kind = display::ComponentKind::Button;
    strncpy(micButton.id, "mic_toggle", sizeof(micButton.id) - 1);
    strncpy(
        micButton.text,
        micMuted ? "Turn Mic On" : "Shut Off Mic",
        sizeof(micButton.text) - 1);
    strncpy(micButton.actionId, "local:mic_toggle", sizeof(micButton.actionId) - 1);

    m_screenBuffer->componentCount = idx;
    return true;
}

void DisplayService::refreshAfterLocalControl() {
    if (m_capture != nullptr && m_capture->isUserMuted()) {
        showMicOff();
        return;
    }
    if (!buildMainScreenModel(m_currentMode, subtitleForMode(m_currentMode), false)) {
        return;
    }
    showScreen(*m_screenBuffer);
}

bool DisplayService::showIdleScreen(const char* deviceName) {
    if (deviceName != nullptr) {
        strncpy(m_idleTitle, deviceName, sizeof(m_idleTitle) - 1);
        m_idleTitle[sizeof(m_idleTitle) - 1] = '\0';
    }
    if (m_capture != nullptr && m_capture->isUserMuted()) {
        return showMicOff();
    }
    if (!buildMainScreenModel(display::ScreenMode::Listening, subtitleForMode(display::ScreenMode::Listening), false)) {
        return false;
    }
    return showScreen(*m_screenBuffer);
}

bool DisplayService::showListening() {
    if (!isReady()) {
        return false;
    }
    if (m_capture != nullptr && m_capture->isUserMuted()) {
        return showMicOff();
    }
    if (!buildMainScreenModel(display::ScreenMode::Listening, subtitleForMode(display::ScreenMode::Listening), false)) {
        return false;
    }
    return showScreen(*m_screenBuffer);
}

bool DisplayService::showRecording() {
    if (!isReady()) {
        return false;
    }
    if (m_capture != nullptr && m_capture->isUserMuted()) {
        return showMicOff();
    }
    if (!buildMainScreenModel(display::ScreenMode::Recording, subtitleForMode(display::ScreenMode::Recording), false)) {
        return false;
    }
    return showScreen(*m_screenBuffer);
}

bool DisplayService::showSpeaking() {
    if (!isReady()) {
        return false;
    }
    if (!buildMainScreenModel(display::ScreenMode::Speaking, subtitleForMode(display::ScreenMode::Speaking), false)) {
        return false;
    }
    return showScreen(*m_screenBuffer);
}

bool DisplayService::showMicOff() {
    if (!isReady()) {
        return false;
    }
    if (!buildMainScreenModel(
            display::ScreenMode::MicOff,
            subtitleForMode(display::ScreenMode::MicOff),
            true)) {
        return false;
    }
    return showScreen(*m_screenBuffer);
}

bool DisplayService::showSimpleScreen(display::ScreenMode mode, const char* title, const char* subtitle) {
    if (m_screenBuffer == nullptr) {
        ESP_LOGW(kTag, "screen buffer not allocated");
        return false;
    }

    display::resetScreenModel(*m_screenBuffer);
    m_screenBuffer->mode = mode;
    if (title != nullptr) {
        strncpy(m_screenBuffer->title, title, sizeof(m_screenBuffer->title) - 1);
    }
    if (subtitle != nullptr) {
        strncpy(m_screenBuffer->subtitle, subtitle, sizeof(m_screenBuffer->subtitle) - 1);
    }
    return showScreen(*m_screenBuffer);
}

bool DisplayService::showParsedScreen(const cJSON* screenObject, char* errorOut, size_t errorOutLen) {
    if (m_screenBuffer == nullptr) {
        if (errorOut != nullptr && errorOutLen > 0) {
            strncpy(errorOut, "display buffer unavailable", errorOutLen - 1);
            errorOut[errorOutLen - 1] = '\0';
        }
        return false;
    }
    if (!m_parser.parseScreen(screenObject, *m_screenBuffer, errorOut, errorOutLen)) {
        return false;
    }
    return showScreen(*m_screenBuffer);
}

void DisplayService::handleLocalButton(const char* actionId) {
    const char* action = localActionName(actionId);
    if (strcmp(action, "mic_toggle") == 0 && m_capture != nullptr) {
        m_capture->setUserMuted(!m_capture->isUserMuted());
        ESP_LOGI(kTag, "mic %s", m_capture->isUserMuted() ? "muted" : "enabled");
    }
}

void DisplayService::handleLocalSlider(const char* actionId, int value) {
    const char* action = localActionName(actionId);
    if (strcmp(action, "volume") == 0 && m_playback != nullptr) {
        if (value < 0) {
            value = 0;
        } else if (value > 100) {
            value = 100;
        }
        m_playback->setVolumePercent(static_cast<uint8_t>(value));
        ESP_LOGI(kTag, "playback volume=%d%%", value);
        refreshAfterLocalControl();
    }
}

void DisplayService::onButtonPressed(const char* componentId, const char* actionId, void* context) {
    auto* self = static_cast<DisplayService*>(context);
    if (self == nullptr || actionId == nullptr) {
        return;
    }

    if (isLocalAction(actionId)) {
        self->handleLocalButton(actionId);
        return;
    }

    if (self->m_uiEventClient != nullptr) {
        self->m_uiEventClient->enqueueButtonPress(componentId, actionId, "");
    }
}

void DisplayService::onSliderChanged(const char* componentId, const char* actionId, int value, void* context) {
    (void)componentId;
    auto* self = static_cast<DisplayService*>(context);
    if (self == nullptr || actionId == nullptr) {
        return;
    }

    if (isLocalAction(actionId)) {
        self->handleLocalSlider(actionId, value);
        return;
    }
}
