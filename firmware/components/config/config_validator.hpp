        /**
         * @file config_validator.hpp
         * @brief Validates config patches and full config objects.
         *
         * Responsibilities:
 * - reject invalid ranges and missing required fields
         *
         * Non-responsibilities:
 * - NVS I/O
 * - REST parsing
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Validates config patches and full config objects.
         */
        class ConfigValidator {
        public:
            /**
     * @brief Validates a partial config patch.
     */
    bool validatePatch();
        };
