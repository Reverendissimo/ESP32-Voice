/**
 * @file lvgl_renderer.hpp
 * @brief Renders screen models with LVGL.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"
#include "screen_model.hpp"

/**
 * @brief Renders declarative screens on the BOX-3 LCD.
 */
class LvglRenderer {
public:
    using ButtonHandler = void (*)(const char* componentId, const char* actionId, void* context);

    bool initialize(uint8_t brightnessPercent);
    bool isReady() const;
    void setButtonHandler(ButtonHandler handler, void* context);

    /**
     * @brief Renders a full-screen update while holding the LVGL mutex.
     */
    bool scheduleRender(const display::ScreenModel& model);

private:
    struct ButtonBinding {
        char componentId[display::kMaxIdLen];
        char actionId[display::kMaxIdLen];
    };

    static void onButtonClicked(lv_event_t* event);

    void clearButtonBindings();
    void renderOnLvglTask(const display::ScreenModel& model);

    bool m_ready = false;
    ButtonHandler m_buttonHandler = nullptr;
    void* m_buttonContext = nullptr;
    ButtonBinding* m_buttonBindings = nullptr;
    uint8_t m_buttonBindingCount = 0;
};
