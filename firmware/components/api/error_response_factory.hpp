        /**
         * @file error_response_factory.hpp
         * @brief Builds structured API error responses.
         *
         * Responsibilities:
 * - stable error schema
 * - request_id propagation
         *
         * Non-responsibilities:
 * - route-specific validation
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Builds structured API error responses.
         */
        class ErrorResponseFactory {
        public:
            /**
     * @brief Builds INVALID_REQUEST payload.
     */
    bool buildInvalidRequest();
        };
