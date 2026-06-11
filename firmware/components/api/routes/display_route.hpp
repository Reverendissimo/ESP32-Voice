        /**
         * @file display_route.hpp
         * @brief Handles POST /api/v1/display.
         *
         * Responsibilities:
 * - Handles POST /api/v1/display.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles POST /api/v1/display.
         */
        class DisplayRoute {
        public:
            /**
     * @brief Registers Handles POST /api/v1/display.
     */
    bool registerRoute();
        };
