        /**
         * @file config_manager.hpp
         * @brief Manages defaults, saved, and active config layers.
         *
         * Responsibilities:
 * - apply patches to active RAM config
 * - revert and explicit save
         *
         * Non-responsibilities:
 * - HTTP request parsing
 * - Wi-Fi driver control
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Manages defaults, saved, and active config layers.
         */
        class ConfigManager {
        public:
            /**
     * @brief Applies patch to active config.
     */
    bool applyPatch();
        };
