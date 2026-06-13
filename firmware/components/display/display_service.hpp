/**
 * @file display_service.hpp
 * @brief Applies declarative display commands to screen.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cJSON.h"
#include "lvgl_renderer.hpp"
#include "screen_model.hpp"
#include "screen_parser.hpp"

class UiEventClient;

/**
 * @brief Owns display initialization and full-screen updates.
 */
class DisplayService {
public:
    void setUiEventClient(UiEventClient* uiEventClient);

    bool initialize(uint8_t brightnessPercent);
    bool isReady() const;
    bool showScreen(const display::ScreenModel& model);
    bool showSimpleScreen(display::ScreenMode mode, const char* title, const char* subtitle);
    bool showParsedScreen(const cJSON* screenObject, char* errorOut, size_t errorOutLen);

    static void onButtonPressed(const char* componentId, const char* actionId, void* context);

private:
    UiEventClient* m_uiEventClient = nullptr;
    ScreenParser m_parser;
    LvglRenderer m_renderer;
    display::ScreenModel* m_screenBuffer = nullptr;
};
