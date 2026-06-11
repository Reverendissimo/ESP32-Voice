        /**
         * @file route_registry.hpp
         * @brief Registers REST route handlers with the HTTP server.
         *
         * Responsibilities:
 * - map paths to thin handlers
         *
         * Non-responsibilities:
 * - hardware access
 * - config validation internals
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Registers REST route handlers with the HTTP server.
         */
        class RouteRegistry {
        public:
            /**
     * @brief Registers all /api/v1 routes.
     */
    bool registerAll();
        };
