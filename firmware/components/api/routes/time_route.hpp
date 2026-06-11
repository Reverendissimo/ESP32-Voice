        /**
         * @file time_route.hpp
         * @brief Handles /api/v1/time routes.
         *
         * Responsibilities:
 * - Handles /api/v1/time routes.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles /api/v1/time routes.
         */
        class TimeRoute {
        public:
            /**
     * @brief Registers Handles /api/v1/time routes.
     */
    bool registerRoute();
        };
