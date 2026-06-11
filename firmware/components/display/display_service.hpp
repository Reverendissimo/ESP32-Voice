        /**
         * @file display_service.hpp
         * @brief Applies declarative display commands to screen.
         *
         * Responsibilities:
 * - full-screen replacement updates
         *
         * Non-responsibilities:
 * - REST parsing
 * - LVGL draw primitives
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Applies declarative display commands to screen.
         */
        class DisplayService {
        public:
            /**
     * @brief Renders a parsed screen model.
     */
    bool showScreen();
        };
