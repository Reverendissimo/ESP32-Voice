        /**
         * @file screen_parser.hpp
         * @brief Parses display JSON into screen models.
         *
         * Responsibilities:
 * - schema validation and rejection
         *
         * Non-responsibilities:
 * - LVGL rendering
 * - HTTP transport
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Parses display JSON into screen models.
         */
        class ScreenParser {
        public:
            /**
     * @brief Parses JSON into ScreenModel.
     */
    bool parse();
        };
