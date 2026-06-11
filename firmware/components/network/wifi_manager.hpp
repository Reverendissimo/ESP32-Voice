        /**
         * @file wifi_manager.hpp
         * @brief Owns Wi-Fi station connection lifecycle.
         *
         * Responsibilities:
 * - connect, disconnect, expose Wi-Fi state
         *
         * Non-responsibilities:
 * - config persistence
 * - HTTP API handling
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Owns Wi-Fi station connection lifecycle.
         */
        class WifiManager {
        public:
            /**
     * @brief Applies Wi-Fi config and connects.
     */
    bool start();
        };
