        /**
         * @file health_route.hpp
         * @brief Handles GET /api/v1/health.
         *
         * Responsibilities:
 * - Handles GET /api/v1/health.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles GET /api/v1/health.
         */
        class HealthRoute {
        public:
            /**
     * @brief Registers Handles GET /api/v1/health.
     */
    bool registerRoute();
        };
