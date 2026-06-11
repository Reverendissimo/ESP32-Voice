        /**
         * @file environment_route.hpp
         * @brief Handles GET /api/v1/environment.
         *
         * Responsibilities:
 * - Handles GET /api/v1/environment.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles GET /api/v1/environment.
         */
        class EnvironmentRoute {
        public:
            /**
     * @brief Registers Handles GET /api/v1/environment.
     */
    bool registerRoute();
        };
