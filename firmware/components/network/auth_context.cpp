/**
 * @file auth_context.cpp
 * @brief Implementation of AuthContext.
 */
#include "auth_context.hpp"

#include <string.h>

void AuthContext::configure(const char* expectedToken) {
    m_expectedToken[0] = '\0';
    m_authRequired = false;

    if (expectedToken == nullptr || expectedToken[0] == '\0') {
        return;
    }

    strncpy(m_expectedToken, expectedToken, sizeof(m_expectedToken) - 1);
    m_expectedToken[sizeof(m_expectedToken) - 1] = '\0';
    m_authRequired = true;
}

bool AuthContext::authorize(const char* providedToken) const {
    if (!m_authRequired) {
        return true;
    }
    if (providedToken == nullptr || providedToken[0] == '\0') {
        return false;
    }
    return strcmp(m_expectedToken, providedToken) == 0;
}

bool AuthContext::extractBearerToken(const char* authorizationHeader, char* tokenOut, size_t tokenOutLen) const {
    if (tokenOut == nullptr || tokenOutLen == 0) {
        return false;
    }
    tokenOut[0] = '\0';

    if (authorizationHeader == nullptr) {
        return false;
    }

    const char* prefix = "Bearer ";
    const size_t prefixLen = strlen(prefix);
    if (strncmp(authorizationHeader, prefix, prefixLen) != 0) {
        return false;
    }

    const char* token = authorizationHeader + prefixLen;
    if (token[0] == '\0') {
        return false;
    }

    strncpy(tokenOut, token, tokenOutLen - 1);
    tokenOut[tokenOutLen - 1] = '\0';
    return true;
}
