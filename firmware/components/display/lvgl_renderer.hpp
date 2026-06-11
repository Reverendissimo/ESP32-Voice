        /**
         * @file lvgl_renderer.hpp
         * @brief Renders screen models with LVGL.
         *
         * Responsibilities:
 * - draw supported component kinds
         *
         * Non-responsibilities:
 * - JSON parsing
 * - REST handlers
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Renders screen models with LVGL.
         */
        class LvglRenderer {
        public:
            /**
     * @brief Renders screen to display.
     */
    bool render();
        };
