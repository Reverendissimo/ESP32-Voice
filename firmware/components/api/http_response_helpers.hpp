/**
 * @file http_response_helpers.hpp
 * @brief Common HTTP response helpers for REST handlers.
 *
 * Responsibilities:
 * - send JSON and read request bodies
 *
 * Non-responsibilities:
 * - route-specific validation
 */
#pragma once

#include <stddef.h>

#include "esp_http_server.h"

/**
 * @brief Reads full HTTP request body into a heap buffer.
 *
 * @param req Incoming request.
 * @param outBody Output pointer; caller must free.
 * @param outLen Output body length.
 */
bool readRequestBody(httpd_req_t* req, char** outBody, size_t* outLen);

/**
 * @brief Sends a JSON HTTP response.
 */
esp_err_t sendJsonResponse(httpd_req_t* req, int statusCode, const char* jsonBody);

/**
 * @brief Extracts request_id from a JSON body when present.
 */
void extractRequestId(const char* jsonBody, char* requestIdOut, size_t requestIdOutLen);
