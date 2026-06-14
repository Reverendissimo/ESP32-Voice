/**
 * @file screen_model.hpp
 * @brief Defines supported declarative screen schema types.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace display {

constexpr size_t kMaxIdLen = 32;
constexpr size_t kMaxTextLen = 256;
constexpr size_t kMaxComponents = 16;
constexpr size_t kMaxListItems = 8;

enum class ScreenMode : uint8_t {
    Idle,
    Listening,
    Recording,
    Speaking,
    MicOff,
    Thinking,
    Message,
    ChoiceList,
    Confirm,
    Notification,
    Form,
    Unknown,
};

enum class ComponentKind : uint8_t {
    Text,
    Badge,
    Button,
    List,
    Input,
    Progress,
    Slider,
    Unknown,
};

struct ListItem {
    char text[kMaxTextLen];
};

struct ScreenComponent {
    ComponentKind kind = ComponentKind::Unknown;
    char id[kMaxIdLen] = {};
    char text[kMaxTextLen] = {};
    char actionId[kMaxIdLen] = {};
    char placeholder[kMaxTextLen] = {};
    uint8_t progressPercent = 0;
    uint8_t listItemCount = 0;
    ListItem listItems[kMaxListItems] = {};
};

struct ScreenModel {
    ScreenMode mode = ScreenMode::Unknown;
    char title[kMaxTextLen];
    char subtitle[kMaxTextLen];
    bool subtitleAlert = false;
    uint8_t componentCount = 0;
    ScreenComponent components[kMaxComponents];

    ScreenModel() {
        title[0] = '\0';
        subtitle[0] = '\0';
        for (uint8_t i = 0; i < kMaxComponents; ++i) {
            components[i] = ScreenComponent{};
        }
    }
};

const char* screenModeToString(ScreenMode mode);
bool screenModeFromString(const char* value, ScreenMode& out);

const char* componentKindToString(ComponentKind kind);
bool componentKindFromString(const char* value, ComponentKind& out);

/**
 * @brief Clears a ScreenModel in place (never assign from a stack temporary).
 */
void resetScreenModel(ScreenModel& model);

/**
 * @brief Allocates a ScreenModel in PSRAM (42+ KiB — never place on task stack).
 */
ScreenModel* allocScreenModelPsram();

/**
 * @brief Frees a model returned by allocScreenModelPsram().
 */
void freeScreenModelPsram(ScreenModel* model);

}  // namespace display
