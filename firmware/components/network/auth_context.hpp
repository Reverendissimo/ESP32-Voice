        /**
         * @file auth_context.hpp
         * @brief Validates auth tokens for protected endpoints.
         *
         * Responsibilities:
 * - token presence and validity checks
         *
         * Non-responsibilities:
 * - user account management
 * - secret logging
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Validates auth tokens for protected endpoints.
         */
        class AuthContext {
        public:
            /**
     * @brief Returns true for valid admin token.
     */
    bool authorize();
        };
