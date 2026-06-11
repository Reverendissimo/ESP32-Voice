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

#include <stddef.h>

/**
 * @brief Builds structured API error JSON payloads.
 */
class ErrorResponseFactory {
public:
    /**
     * @brief Builds a standard error JSON object into outBuffer.
     *
     * @return true when the response fit in outBuffer.
     */
    bool build(
        const char* code,
        const char* message,
        bool retryable,
        const char* requestId,
        char* outBuffer,
        size_t outBufferLen) const;
};
