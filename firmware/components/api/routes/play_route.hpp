        /**
         * @file play_route.hpp
         * @brief Handles POST /api/v1/play.
         *
         * Responsibilities:
 * - Handles POST /api/v1/play.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles POST /api/v1/play.
         */
        class PlayRoute {
        public:
            /**
     * @brief Registers Handles POST /api/v1/play.
     */
    bool registerRoute();
        };
