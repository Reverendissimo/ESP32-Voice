        /**
         * @file cli_command_registry.hpp
         * @brief Registers and dispatches CLI commands.
         *
         * Responsibilities:
 * - help, status, config, wifi commands
         *
         * Non-responsibilities:
 * - NVS driver access
 * - Wi-Fi driver internals
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Registers and dispatches CLI commands.
         */
        class CliCommandRegistry {
        public:
            /**
     * @brief Registers required CLI commands.
     */
    bool registerDefaults();
        };
