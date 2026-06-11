/**
 * @file error_response_factory.cpp
 * @brief Implementation of ErrorResponseFactory.
 */
#include "error_response_factory.hpp"

#include <stdio.h>
#include <string.h>

bool ErrorResponseFactory::build(
    const char* code,
    const char* message,
    bool retryable,
    const char* requestId,
    char* outBuffer,
    size_t outBufferLen) const {
    if (outBuffer == nullptr || outBufferLen == 0 || code == nullptr || message == nullptr) {
        return false;
    }

    const char* reqId = (requestId != nullptr && requestId[0] != '\0') ? requestId : "";
    const int written = snprintf(
        outBuffer,
        outBufferLen,
        "{\"v\":1,\"error\":{\"code\":\"%s\",\"message\":\"%s\",\"retryable\":%s,\"request_id\":\"%s\"}}",
        code,
        message,
        retryable ? "true" : "false",
        reqId);

    return written > 0 && static_cast<size_t>(written) < outBufferLen;
}
