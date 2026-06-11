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

#include <cstddef>
#include <stdbool.h>

/**
 * @brief Auth token validation for admin/control endpoints.
 */
class AuthContext {
public:
    /**
     * @brief Configures expected token; empty token disables auth checks.
     */
    void configure(const char* expectedToken);

    /**
     * @brief Returns true when provided token is valid for protected endpoints.
     */
    bool authorize(const char* providedToken) const;

    /**
     * @brief Extracts bearer token from Authorization header value.
     */
    bool extractBearerToken(const char* authorizationHeader, char* tokenOut, size_t tokenOutLen) const;

private:
    char m_expectedToken[64] = {};
    bool m_authRequired = false;
};
