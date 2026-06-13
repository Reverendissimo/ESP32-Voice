/**
 * @file screen_parser.hpp
 * @brief Parses display JSON into screen models.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "screen_model.hpp"

struct cJSON;

/**
 * @brief Parses declarative display JSON into ScreenModel.
 */
class ScreenParser {
public:
    bool parseScreen(
        const cJSON* screenObject,
        display::ScreenModel& out,
        char* errorOut,
        size_t errorOutLen) const;
};
