        /**
         * @file serial_cli_service.hpp
         * @brief Owns USB serial CLI session lifecycle.
         *
         * Responsibilities:
 * - read commands and print human output
         *
         * Non-responsibilities:
 * - config validation internals
 * - HTTP server
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Owns USB serial CLI session lifecycle.
         */
        class SerialCliService {
        public:
            /**
     * @brief Starts serial CLI loop.
     */
    bool start();
        };
