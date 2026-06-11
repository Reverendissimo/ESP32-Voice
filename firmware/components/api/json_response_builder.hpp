        /**
         * @file json_response_builder.hpp
         * @brief Builds consistent JSON success responses.
         *
         * Responsibilities:
 * - include version and identity fields
         *
         * Non-responsibilities:
 * - business rule evaluation
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Builds consistent JSON success responses.
         */
        class JsonResponseBuilder {
        public:
            /**
     * @brief Builds health JSON payload.
     */
    bool buildHealth();
        };
