/**
 * @file screen_parser.cpp
 * @brief Implementation of ScreenParser.
 */
#include "screen_parser.hpp"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

namespace {

void setError(char* errorOut, size_t errorOutLen, const char* message) {
    if (errorOut == nullptr || errorOutLen == 0 || message == nullptr) {
        return;
    }
    strncpy(errorOut, message, errorOutLen - 1);
    errorOut[errorOutLen - 1] = '\0';
}

void copyBounded(char* dest, size_t destSize, const char* value) {
    if (dest == nullptr || destSize == 0) {
        return;
    }
    if (value == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, destSize - 1);
    dest[destSize - 1] = '\0';
}

bool parseListItems(const cJSON* itemsNode, display::ScreenComponent& component, char* errorOut, size_t errorOutLen) {
    if (!cJSON_IsArray(itemsNode)) {
        setError(errorOut, errorOutLen, "list.items must be an array");
        return false;
    }

    const int count = cJSON_GetArraySize(itemsNode);
    if (count > static_cast<int>(display::kMaxListItems)) {
        setError(errorOut, errorOutLen, "list.items exceeds max items");
        return false;
    }

    component.listItemCount = 0;
    for (int i = 0; i < count; ++i) {
        const cJSON* itemNode = cJSON_GetArrayItem(itemsNode, i);
        if (!cJSON_IsObject(itemNode)) {
            setError(errorOut, errorOutLen, "list item must be an object");
            return false;
        }
        const cJSON* textNode = cJSON_GetObjectItemCaseSensitive(itemNode, "text");
        if (!cJSON_IsString(textNode)) {
            setError(errorOut, errorOutLen, "list item missing text");
            return false;
        }
        copyBounded(
            component.listItems[component.listItemCount].text,
            sizeof(component.listItems[component.listItemCount].text),
            textNode->valuestring);
        ++component.listItemCount;
    }
    return true;
}

bool parseComponent(const cJSON* componentNode, display::ScreenComponent& component, char* errorOut, size_t errorOutLen) {
    if (!cJSON_IsObject(componentNode)) {
        setError(errorOut, errorOutLen, "component must be an object");
        return false;
    }

    const cJSON* kindNode = cJSON_GetObjectItemCaseSensitive(componentNode, "kind");
    if (!cJSON_IsString(kindNode) || !display::componentKindFromString(kindNode->valuestring, component.kind)) {
        setError(errorOut, errorOutLen, "unsupported component kind");
        return false;
    }

    const cJSON* idNode = cJSON_GetObjectItemCaseSensitive(componentNode, "id");
    if (cJSON_IsString(idNode)) {
        copyBounded(component.id, sizeof(component.id), idNode->valuestring);
    }

    const cJSON* textNode = cJSON_GetObjectItemCaseSensitive(componentNode, "text");
    if (cJSON_IsString(textNode)) {
        copyBounded(component.text, sizeof(component.text), textNode->valuestring);
    }

    const cJSON* actionNode = cJSON_GetObjectItemCaseSensitive(componentNode, "action_id");
    if (cJSON_IsString(actionNode)) {
        copyBounded(component.actionId, sizeof(component.actionId), actionNode->valuestring);
    }

    const cJSON* placeholderNode = cJSON_GetObjectItemCaseSensitive(componentNode, "placeholder");
    if (cJSON_IsString(placeholderNode)) {
        copyBounded(component.placeholder, sizeof(component.placeholder), placeholderNode->valuestring);
    }

    const cJSON* percentNode = cJSON_GetObjectItemCaseSensitive(componentNode, "percent");
    if (cJSON_IsNumber(percentNode)) {
        const int percent = percentNode->valueint;
        if (percent < 0 || percent > 100) {
            setError(errorOut, errorOutLen, "progress.percent out of range");
            return false;
        }
        component.progressPercent = static_cast<uint8_t>(percent);
    }

    if (component.kind == display::ComponentKind::List) {
        const cJSON* itemsNode = cJSON_GetObjectItemCaseSensitive(componentNode, "items");
        if (!parseListItems(itemsNode, component, errorOut, errorOutLen)) {
            return false;
        }
    }

    if (component.kind == display::ComponentKind::Button && component.text[0] == '\0') {
        setError(errorOut, errorOutLen, "button requires text");
        return false;
    }

    return true;
}

}  // namespace

bool ScreenParser::parseScreen(
    const cJSON* screenObject,
    display::ScreenModel& out,
    char* errorOut,
    size_t errorOutLen) const {
    if (screenObject == nullptr || !cJSON_IsObject(screenObject)) {
        setError(errorOut, errorOutLen, "screen must be an object");
        return false;
    }

    display::resetScreenModel(out);

    const cJSON* modeNode = cJSON_GetObjectItemCaseSensitive(screenObject, "mode");
    if (!cJSON_IsString(modeNode) || !display::screenModeFromString(modeNode->valuestring, out.mode)) {
        setError(errorOut, errorOutLen, "unsupported or missing screen.mode");
        return false;
    }

    const cJSON* titleNode = cJSON_GetObjectItemCaseSensitive(screenObject, "title");
    if (cJSON_IsString(titleNode)) {
        copyBounded(out.title, sizeof(out.title), titleNode->valuestring);
    }

    const cJSON* subtitleNode = cJSON_GetObjectItemCaseSensitive(screenObject, "subtitle");
    if (cJSON_IsString(subtitleNode)) {
        copyBounded(out.subtitle, sizeof(out.subtitle), subtitleNode->valuestring);
    }

    const cJSON* componentsNode = cJSON_GetObjectItemCaseSensitive(screenObject, "components");
    if (componentsNode != nullptr) {
        if (!cJSON_IsArray(componentsNode)) {
            setError(errorOut, errorOutLen, "screen.components must be an array");
            return false;
        }

        const int count = cJSON_GetArraySize(componentsNode);
        if (count > static_cast<int>(display::kMaxComponents)) {
            setError(errorOut, errorOutLen, "screen.components exceeds max components");
            return false;
        }

        for (int i = 0; i < count; ++i) {
            if (!parseComponent(
                    cJSON_GetArrayItem(componentsNode, i),
                    out.components[out.componentCount],
                    errorOut,
                    errorOutLen)) {
                return false;
            }
            ++out.componentCount;
        }
    }

    return true;
}
