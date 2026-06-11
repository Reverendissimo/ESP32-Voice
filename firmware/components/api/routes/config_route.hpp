        /**
         * @file config_route.hpp
         * @brief Handles /api/v1/config routes.
         *
         * Responsibilities:
 * - Handles /api/v1/config routes.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles /api/v1/config routes.
         */
        class ConfigRoute {
        public:
            /**
     * @brief Registers Handles /api/v1/config routes.
     */
    bool registerRoute();
        };
