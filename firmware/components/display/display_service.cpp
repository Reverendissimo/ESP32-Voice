/**
 * @file display_service.cpp
 * @brief Implementation of DisplayService.
 */
#include "display_service.hpp"

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "ui_event_client.hpp"

static const char* kTag = "display_service";

void DisplayService::setUiEventClient(UiEventClient* uiEventClient) {
    m_uiEventClient = uiEventClient;
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

void DisplayService::onButtonPressed(const char* componentId, const char* actionId, void* context) {
    auto* self = static_cast<DisplayService*>(context);
    if (self == nullptr || self->m_uiEventClient == nullptr) {
        return;
    }
    self->m_uiEventClient->enqueueButtonPress(componentId, actionId, "");
}
