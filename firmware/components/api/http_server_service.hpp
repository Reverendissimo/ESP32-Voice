        /**
         * @file http_server_service.hpp
         * @brief Owns local HTTP server lifecycle.
         *
         * Responsibilities:
 * - start/stop esp_http_server
 * - delegate routes to registry
         *
         * Non-responsibilities:
 * - per-route business logic
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Owns local HTTP server lifecycle.
         */
        class HttpServerService {
        public:
            /**
     * @brief Starts HTTP server on configured port.
     */
    bool start();
        };
