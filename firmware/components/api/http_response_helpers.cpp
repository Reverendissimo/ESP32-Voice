/**
 * @file http_response_helpers.cpp
 * @brief Implementation of HTTP response helpers.
 */
#include "http_response_helpers.hpp"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

bool readRequestBody(httpd_req_t* req, char** outBody, size_t* outLen) {
    if (req == nullptr || outBody == nullptr || outLen == nullptr) {
        return false;
    }

    *outBody = nullptr;
    *outLen = 0;

    if (req->content_len == 0) {
        char* empty = static_cast<char*>(malloc(1));
        if (empty == nullptr) {
            return false;
        }
        empty[0] = '\0';
        *outBody = empty;
        return true;
    }

    char* buffer = static_cast<char*>(malloc(req->content_len + 1));
    if (buffer == nullptr) {
        return false;
    }

    size_t received = 0;
    while (received < req->content_len) {
        const int chunk = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (chunk <= 0) {
            free(buffer);
            return false;
        }
        received += static_cast<size_t>(chunk);
    }

    buffer[received] = '\0';
    *outBody = buffer;
    *outLen = received;
    return true;
}

esp_err_t sendJsonResponse(httpd_req_t* req, int statusCode, const char* jsonBody) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, statusCode == 200 ? "200 OK" : "400 Bad Request");
    if (statusCode == 401) {
        httpd_resp_set_status(req, "401 Unauthorized");
    } else if (statusCode == 404) {
        httpd_resp_set_status(req, "404 Not Found");
    } else if (statusCode == 501) {
        httpd_resp_set_status(req, "501 Not Implemented");
    } else if (statusCode == 503) {
        httpd_resp_set_status(req, "503 Service Unavailable");
    } else if (statusCode == 202) {
        httpd_resp_set_status(req, "202 Accepted");
    } else if (statusCode >= 500) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    }
    return httpd_resp_send(req, jsonBody, HTTPD_RESP_USE_STRLEN);
}

void extractRequestId(const char* jsonBody, char* requestIdOut, size_t requestIdOutLen) {
    if (requestIdOut == nullptr || requestIdOutLen == 0) {
        return;
    }
    requestIdOut[0] = '\0';
    if (jsonBody == nullptr) {
        return;
    }

    cJSON* root = cJSON_Parse(jsonBody);
    if (root == nullptr) {
        return;
    }

    const cJSON* requestId = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    if (cJSON_IsString(requestId) && requestId->valuestring != nullptr) {
        strncpy(requestIdOut, requestId->valuestring, requestIdOutLen - 1);
        requestIdOut[requestIdOutLen - 1] = '\0';
    }
    cJSON_Delete(root);
}
