/**
 * @file play_route.cpp
 * @brief Implementation of PlayRoute.
 */
#include "play_route.hpp"

#include <stdlib.h>
#include <string.h>

#include "api_context.hpp"
#include "audio_playback_service.hpp"
#include "auth_context.hpp"
#include "cJSON.h"
#include "error_response_factory.hpp"
#include "http_response_helpers.hpp"
#include "mbedtls/base64.h"

static constexpr size_t kRequestBodyMax = 8192;

namespace {

bool readBody(httpd_req_t* req, char* buffer, size_t bufferLen, size_t& outLen) {
    outLen = 0;
    if (req == nullptr || buffer == nullptr || bufferLen == 0) {
        return false;
    }

    const int total = req->content_len;
    if (total <= 0 || static_cast<size_t>(total) >= bufferLen) {
        return false;
    }

    int received = 0;
    while (received < total) {
        const int chunk = httpd_req_recv(req, buffer + received, total - received);
        if (chunk <= 0) {
            return false;
        }
        received += chunk;
    }
    buffer[received] = '\0';
    outLen = static_cast<size_t>(received);
    return true;
}

bool extractAuthToken(httpd_req_t* req, const ApiContext* context, char* tokenOut, size_t tokenOutLen) {
    if (req == nullptr || context == nullptr || context->authContext == nullptr || tokenOut == nullptr) {
        return false;
    }

    char authHeader[128] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", authHeader, sizeof(authHeader)) == ESP_OK) {
        return context->authContext->extractBearerToken(authHeader, tokenOut, tokenOutLen);
    }

    char headerToken[64] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Auth-Token", headerToken, sizeof(headerToken)) == ESP_OK) {
        strncpy(tokenOut, headerToken, tokenOutLen - 1);
        tokenOut[tokenOutLen - 1] = '\0';
        return true;
    }

    tokenOut[0] = '\0';
    return false;
}

bool requireAuth(httpd_req_t* req, const ApiContext* context, const char* requestId) {
    char token[64] = {};
    extractAuthToken(req, context, token, sizeof(token));
    if (context == nullptr || context->authContext == nullptr || !context->authContext->authorize(token)) {
        ErrorResponseFactory errors;
        char body[256] = {};
        errors.build("UNAUTHORIZED", "Missing or invalid auth token", false, requestId, body, sizeof(body));
        sendJsonResponse(req, 401, body);
        return false;
    }
    return true;
}

esp_err_t sendOk(httpd_req_t* req, const char* requestId, const char* commandId) {
    char body[256];
    snprintf(
        body,
        sizeof(body),
        "{\"v\":1,\"ok\":true,\"request_id\":\"%s\",\"command_id\":\"%s\"}",
        requestId != nullptr ? requestId : "",
        commandId != nullptr ? commandId : "");
    return sendJsonResponse(req, 200, body);
}

}  // namespace

bool PlayRoute::registerRoutes(httpd_handle_t server, const ApiContext* context) const {
    if (server == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t playUri = {
        .uri = "/api/v1/play",
        .method = HTTP_POST,
        .handler = handlePost,
        .user_ctx = const_cast<ApiContext*>(context),
    };
    return httpd_register_uri_handler(server, &playUri) == ESP_OK;
}

esp_err_t PlayRoute::handlePost(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char* body = static_cast<char*>(malloc(kRequestBodyMax));
    if (body == nullptr) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Out of memory", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    size_t bodyLen = 0;
    if (!readBody(req, body, kRequestBodyMax, bodyLen)) {
        free(body);
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Request body too large or unreadable", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    cJSON* root = cJSON_Parse(body);
    if (root == nullptr) {
        free(body);
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Malformed JSON", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }
    free(body);

    const cJSON* requestIdNode = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    const char* requestId = cJSON_IsString(requestIdNode) ? requestIdNode->valuestring : "";

    if (!requireAuth(req, context, requestId)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    const cJSON* targetNode = cJSON_GetObjectItemCaseSensitive(root, "target_device_uid");
    if (!cJSON_IsString(targetNode) || context->deviceUid == nullptr ||
        strcmp(targetNode->valuestring, context->deviceUid) != 0) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "target_device_uid mismatch", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        return sendJsonResponse(req, 400, errBody);
    }

    if (context->audioPlayback == nullptr || !context->audioPlayback->isRunning()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback service not running", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        return sendJsonResponse(req, 503, errBody);
    }

    const cJSON* pcmNode = cJSON_GetObjectItemCaseSensitive(root, "pcm_b64");
    if (!cJSON_IsString(pcmNode) || pcmNode->valuestring == nullptr) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Missing pcm_b64", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        return sendJsonResponse(req, 400, errBody);
    }

    uint16_t sampleRateHz = 16000;
    uint8_t channels = 1;
    const cJSON* rateNode = cJSON_GetObjectItemCaseSensitive(root, "sample_rate_hz");
    const cJSON* channelsNode = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (cJSON_IsNumber(rateNode)) {
        sampleRateHz = static_cast<uint16_t>(rateNode->valueint);
    }
    if (cJSON_IsNumber(channelsNode)) {
        channels = static_cast<uint8_t>(channelsNode->valueint);
    }

    const size_t b64Len = strlen(pcmNode->valuestring);
    size_t decodedMax = (b64Len * 3) / 4 + 4;
    auto* decoded = static_cast<uint8_t*>(malloc(decodedMax));
    if (decoded == nullptr) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Out of memory", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        return sendJsonResponse(req, 500, errBody);
    }

    size_t decodedLen = 0;
    const int rc = mbedtls_base64_decode(decoded, decodedMax, &decodedLen,
                                         reinterpret_cast<const unsigned char*>(pcmNode->valuestring), b64Len);
    if (rc != 0 || decodedLen < sizeof(int16_t) || (decodedLen % sizeof(int16_t)) != 0) {
        free(decoded);
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Invalid pcm_b64 payload", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        return sendJsonResponse(req, 400, errBody);
    }

    const cJSON* commandIdNode = cJSON_GetObjectItemCaseSensitive(root, "command_id");
    const char* commandId = cJSON_IsString(commandIdNode) ? commandIdNode->valuestring : "";

    const size_t sampleCount = decodedLen / sizeof(int16_t);
    const bool queued = context->audioPlayback->enqueuePcm(
        reinterpret_cast<const int16_t*>(decoded), sampleCount, sampleRateHz, channels);
    free(decoded);
    cJSON_Delete(root);

    if (!queued) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback queue full", true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    return sendOk(req, requestId, commandId);
}
