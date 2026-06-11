        /**
         * @file ir_route.hpp
         * @brief Handles /api/v1/ir routes.
         *
         * Responsibilities:
 * - Handles /api/v1/ir routes.
         *
         * Non-responsibilities:
 * - business logic in services
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Handles /api/v1/ir routes.
         */
        class IrRoute {
        public:
            /**
     * @brief Registers Handles /api/v1/ir routes.
     */
    bool registerRoute();
        };
