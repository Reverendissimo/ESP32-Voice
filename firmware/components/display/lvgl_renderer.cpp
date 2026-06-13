/**
 * @file lvgl_renderer.cpp
 * @brief Implementation of LvglRenderer.
 *
 * Follows ESP-BSP BOX-3 display init (esp-box-3 API.md / display example):
 *   1. bsp_display_start_with_config()
 *   2. bsp_display_brightness_set()
 *   3. bsp_display_lock() -> LVGL widget tree -> bsp_display_unlock()
 */
#include "lvgl_renderer.hpp"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-box-3.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char* kTag = "lvgl_renderer";

void LvglRenderer::onButtonClicked(lv_event_t* event) {
    if (event == nullptr) {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* renderer = static_cast<LvglRenderer*>(lv_obj_get_user_data(target));
    auto* binding = static_cast<ButtonBinding*>(lv_event_get_user_data(event));
    if (binding == nullptr || renderer == nullptr || renderer->m_buttonHandler == nullptr) {
        return;
    }
    renderer->m_buttonHandler(binding->componentId, binding->actionId, renderer->m_buttonContext);
}

void LvglRenderer::onSliderReleased(lv_event_t* event) {
    if (event == nullptr) {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* renderer = static_cast<LvglRenderer*>(lv_obj_get_user_data(target));
    auto* binding = static_cast<SliderBinding*>(lv_event_get_user_data(event));
    if (binding == nullptr || renderer == nullptr || renderer->m_sliderHandler == nullptr) {
        return;
    }
    const int value = static_cast<int>(lv_slider_get_value(target));
    renderer->m_sliderHandler(binding->componentId, binding->actionId, value, renderer->m_sliderContext);
}

bool LvglRenderer::initialize(uint8_t brightnessPercent) {
    if (m_ready) {
        return true;
    }

    // Match esp-box-3 bsp_display_start() defaults (DMA internal buffer).
    // Display must be initialized before Wi-Fi starts (see app_bootstrap.cpp).
    lvgl_port_cfg_t lvglCfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvglCfg.task_stack = 7168;
    lvglCfg.task_affinity = 0;

    constexpr uint32_t kDrawBufLines = 40;
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = lvglCfg,
        .buffer_size = BSP_LCD_H_RES * kDrawBufLines,
        .double_buffer = false,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        },
    };

    lv_display_t* display = bsp_display_start_with_config(&cfg);
    if (display == nullptr) {
        ESP_LOGE(kTag, "bsp_display_start_with_config failed");
        return false;
    }

    // display_audio_photo pattern: brightness only (backlight_on() forces 100%).
    if (bsp_display_brightness_set(static_cast<int>(brightnessPercent)) != ESP_OK) {
        ESP_LOGW(kTag, "brightness set failed, using default");
    }

    m_ready = true;
    ESP_LOGI(
        kTag,
        "display ready brightness=%u buf_lines=%d heap_int=%lu heap_psram=%lu",
        static_cast<unsigned>(brightnessPercent),
        static_cast<int>(kDrawBufLines),
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return true;
}

bool LvglRenderer::isReady() const {
    return m_ready;
}

void LvglRenderer::setButtonHandler(ButtonHandler handler, void* context) {
    m_buttonHandler = handler;
    m_buttonContext = context;
}

void LvglRenderer::setSliderHandler(SliderHandler handler, void* context) {
    m_sliderHandler = handler;
    m_sliderContext = context;
}

bool LvglRenderer::scheduleRender(const display::ScreenModel& model) {
    if (!m_ready) {
        return false;
    }

    // All LVGL calls must run under the BSP mutex (esp-box-3.h / esp_lvgl_port README).
    if (!bsp_display_lock(0)) {
        ESP_LOGW(kTag, "display lock failed");
        return false;
    }

    renderOnLvglTask(model);
    bsp_display_unlock();

    ESP_LOGI(
        kTag,
        "rendered mode=%s components=%u",
        display::screenModeToString(model.mode),
        static_cast<unsigned>(model.componentCount));
    return true;
}

void LvglRenderer::clearButtonBindings() {
    if (m_buttonBindings != nullptr) {
        heap_caps_free(m_buttonBindings);
        m_buttonBindings = nullptr;
    }
    m_buttonBindingCount = 0;
}

void LvglRenderer::clearSliderBindings() {
    if (m_sliderBindings != nullptr) {
        heap_caps_free(m_sliderBindings);
        m_sliderBindings = nullptr;
    }
    m_sliderBindingCount = 0;
}

void LvglRenderer::renderOnLvglTask(const display::ScreenModel& model) {
    clearButtonBindings();
    clearSliderBindings();

    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(screen, 8, 0);

    char modeLabel[64];
    snprintf(modeLabel, sizeof(modeLabel), "%s", display::screenModeToString(model.mode));
    lv_obj_t* badge = lv_label_create(screen);
    lv_label_set_text(badge, modeLabel);
    lv_obj_set_style_text_color(badge, lv_color_hex(0x8BC34A), 0);

    if (model.title[0] != '\0') {
        lv_obj_t* title = lv_label_create(screen);
        lv_label_set_text(title, model.title);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    }

    if (model.subtitle[0] != '\0') {
        lv_obj_t* subtitle = lv_label_create(screen);
        lv_label_set_text(subtitle, model.subtitle);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0xB0BEC5), 0);
        lv_obj_set_width(subtitle, LV_PCT(100));
        lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    }

    uint8_t buttonCount = 0;
    uint8_t sliderCount = 0;
    for (uint8_t i = 0; i < model.componentCount; ++i) {
        if (model.components[i].kind == display::ComponentKind::Button) {
            ++buttonCount;
        } else if (model.components[i].kind == display::ComponentKind::Slider) {
            ++sliderCount;
        }
    }

    if (buttonCount > 0) {
        m_buttonBindings = static_cast<ButtonBinding*>(
            heap_caps_malloc(sizeof(ButtonBinding) * buttonCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (m_buttonBindings == nullptr) {
            ESP_LOGW(kTag, "button binding alloc failed");
            buttonCount = 0;
        }
    }

    if (sliderCount > 0) {
        m_sliderBindings = static_cast<SliderBinding*>(
            heap_caps_malloc(sizeof(SliderBinding) * sliderCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (m_sliderBindings == nullptr) {
            ESP_LOGW(kTag, "slider binding alloc failed");
            sliderCount = 0;
        }
    }

    for (uint8_t i = 0; i < model.componentCount; ++i) {
        const display::ScreenComponent& component = model.components[i];
        switch (component.kind) {
            case display::ComponentKind::Text: {
                lv_obj_t* label = lv_label_create(screen);
                lv_label_set_text(label, component.text);
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
                lv_obj_set_width(label, LV_PCT(100));
                lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
                break;
            }
            case display::ComponentKind::Badge: {
                lv_obj_t* label = lv_label_create(screen);
                lv_label_set_text(label, component.text);
                lv_obj_set_style_text_color(label, lv_color_hex(0x4FC3F7), 0);
                break;
            }
            case display::ComponentKind::Button: {
                lv_obj_t* button = lv_button_create(screen);
                lv_obj_set_width(button, LV_PCT(100));
                lv_obj_t* label = lv_label_create(button);
                lv_label_set_text(label, component.text);
                if (m_buttonBindings != nullptr && m_buttonBindingCount < buttonCount) {
                    ButtonBinding* binding = &m_buttonBindings[m_buttonBindingCount++];
                    strncpy(binding->componentId, component.id, sizeof(binding->componentId) - 1);
                    strncpy(binding->actionId, component.actionId, sizeof(binding->actionId) - 1);
                    lv_obj_add_event_cb(button, onButtonClicked, LV_EVENT_CLICKED, binding);
                    lv_obj_set_user_data(button, this);
                }
                break;
            }
            case display::ComponentKind::List: {
                for (uint8_t itemIndex = 0; itemIndex < component.listItemCount; ++itemIndex) {
                    lv_obj_t* item = lv_label_create(screen);
                    lv_label_set_text(item, component.listItems[itemIndex].text);
                    lv_obj_set_style_text_color(item, lv_color_white(), 0);
                    lv_obj_set_width(item, LV_PCT(100));
                }
                break;
            }
            case display::ComponentKind::Input: {
                lv_obj_t* input = lv_textarea_create(screen);
                lv_obj_set_width(input, LV_PCT(100));
                lv_textarea_set_one_line(input, true);
                if (component.placeholder[0] != '\0') {
                    lv_textarea_set_placeholder_text(input, component.placeholder);
                }
                if (component.text[0] != '\0') {
                    lv_textarea_set_text(input, component.text);
                }
                break;
            }
            case display::ComponentKind::Progress: {
                lv_obj_t* bar = lv_bar_create(screen);
                lv_obj_set_width(bar, LV_PCT(100));
                lv_bar_set_range(bar, 0, 100);
                lv_bar_set_value(bar, component.progressPercent, LV_ANIM_OFF);
                break;
            }
            case display::ComponentKind::Slider: {
                lv_obj_t* label = lv_label_create(screen);
                lv_label_set_text(label, component.text);
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
                lv_obj_t* slider = lv_slider_create(screen);
                lv_obj_set_width(slider, LV_PCT(100));
                lv_slider_set_range(slider, 0, 100);
                lv_slider_set_value(slider, component.progressPercent, LV_ANIM_OFF);
                if (m_sliderBindings != nullptr && m_sliderBindingCount < sliderCount) {
                    SliderBinding* binding = &m_sliderBindings[m_sliderBindingCount++];
                    strncpy(binding->componentId, component.id, sizeof(binding->componentId) - 1);
                    strncpy(binding->actionId, component.actionId, sizeof(binding->actionId) - 1);
                    lv_obj_add_event_cb(slider, onSliderReleased, LV_EVENT_RELEASED, binding);
                    lv_obj_set_user_data(slider, this);
                }
                break;
            }
            default:
                break;
        }
    }
}
