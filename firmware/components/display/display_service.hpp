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

class AudioCaptureService;
class AudioPlaybackService;
class UiEventClient;

/**
 * @brief Owns display initialization and full-screen updates.
 */
class DisplayService {
public:
    void setUiEventClient(UiEventClient* uiEventClient);
    void configureLocalControls(AudioCaptureService* capture, AudioPlaybackService* playback);

    bool initialize(uint8_t brightnessPercent);
    bool isReady() const;
    bool showScreen(const display::ScreenModel& model);
    bool showSimpleScreen(display::ScreenMode mode, const char* title, const char* subtitle);
    bool showIdleScreen(const char* deviceName);
    bool showParsedScreen(const cJSON* screenObject, char* errorOut, size_t errorOutLen);

    static void onButtonPressed(const char* componentId, const char* actionId, void* context);
    static void onSliderChanged(const char* componentId, const char* actionId, int value, void* context);

private:
    bool buildIdleScreenModel(const char* deviceName);
    void handleLocalButton(const char* actionId);
    void handleLocalSlider(const char* actionId, int value);

    UiEventClient* m_uiEventClient = nullptr;
    AudioCaptureService* m_capture = nullptr;
    AudioPlaybackService* m_playback = nullptr;
    ScreenParser m_parser;
    LvglRenderer m_renderer;
    display::ScreenModel* m_screenBuffer = nullptr;
    char m_idleTitle[display::kMaxTextLen] = {};
};
