/**
 * @file screen_model.cpp
 * @brief Screen schema string helpers.
 */
#include "screen_model.hpp"

#include <new>
#include <string.h>

#include "esp_heap_caps.h"

namespace display {

const char* screenModeToString(ScreenMode mode) {
    switch (mode) {
        case ScreenMode::Idle:
            return "idle";
        case ScreenMode::Listening:
            return "listening";
        case ScreenMode::Thinking:
            return "thinking";
        case ScreenMode::Message:
            return "message";
        case ScreenMode::ChoiceList:
            return "choice_list";
        case ScreenMode::Confirm:
            return "confirm";
        case ScreenMode::Notification:
            return "notification";
        case ScreenMode::Form:
            return "form";
        default:
            return "unknown";
    }
}

bool screenModeFromString(const char* value, ScreenMode& out) {
    if (value == nullptr) {
        return false;
    }
    if (strcmp(value, "idle") == 0) {
        out = ScreenMode::Idle;
        return true;
    }
    if (strcmp(value, "listening") == 0) {
        out = ScreenMode::Listening;
        return true;
    }
    if (strcmp(value, "thinking") == 0) {
        out = ScreenMode::Thinking;
        return true;
    }
    if (strcmp(value, "message") == 0) {
        out = ScreenMode::Message;
        return true;
    }
    if (strcmp(value, "choice_list") == 0) {
        out = ScreenMode::ChoiceList;
        return true;
    }
    if (strcmp(value, "confirm") == 0) {
        out = ScreenMode::Confirm;
        return true;
    }
    if (strcmp(value, "notification") == 0) {
        out = ScreenMode::Notification;
        return true;
    }
    if (strcmp(value, "form") == 0) {
        out = ScreenMode::Form;
        return true;
    }
    return false;
}

const char* componentKindToString(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Text:
            return "text";
        case ComponentKind::Badge:
            return "badge";
        case ComponentKind::Button:
            return "button";
        case ComponentKind::List:
            return "list";
        case ComponentKind::Input:
            return "input";
        case ComponentKind::Progress:
            return "progress";
        case ComponentKind::Slider:
            return "slider";
        default:
            return "unknown";
    }
}

bool componentKindFromString(const char* value, ComponentKind& out) {
    if (value == nullptr) {
        return false;
    }
    if (strcmp(value, "text") == 0) {
        out = ComponentKind::Text;
        return true;
    }
    if (strcmp(value, "badge") == 0) {
        out = ComponentKind::Badge;
        return true;
    }
    if (strcmp(value, "button") == 0) {
        out = ComponentKind::Button;
        return true;
    }
    if (strcmp(value, "list") == 0) {
        out = ComponentKind::List;
        return true;
    }
    if (strcmp(value, "input") == 0) {
        out = ComponentKind::Input;
        return true;
    }
    if (strcmp(value, "progress") == 0) {
        out = ComponentKind::Progress;
        return true;
    }
    if (strcmp(value, "slider") == 0) {
        out = ComponentKind::Slider;
        return true;
    }
    return false;
}

void resetScreenModel(ScreenModel& model) {
    memset(&model, 0, sizeof(model));
}

ScreenModel* allocScreenModelPsram() {
    void* memory = heap_caps_malloc(sizeof(ScreenModel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) {
        return nullptr;
    }
    return new (memory) ScreenModel();
}

void freeScreenModelPsram(ScreenModel* model) {
    if (model == nullptr) {
        return;
    }
    model->~ScreenModel();
    heap_caps_free(model);
}

}  // namespace display
