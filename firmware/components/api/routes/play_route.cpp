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
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "http_response_helpers.hpp"
#include "mbedtls/base64.h"

#include "audio_types.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// JSON + base64 overhead for 24 KiB PCM chunks; binary PCM uses pcm buffer only.
static constexpr size_t kJsonBodyMax = 36 * 1024;
static constexpr size_t kDecodedPcmMax = audio::kMaxPlaybackBytes;

static const char* kTag = "play_route";

namespace {

struct PlayBuffers {
    uint8_t* pcm = nullptr;
    char* jsonBody = nullptr;
    SemaphoreHandle_t mutex = nullptr;
};

PlayBuffers& playBuffers() {
    static PlayBuffers buffers;
    return buffers;
}

bool ensureMutex() {
    auto& buffers = playBuffers();
    if (buffers.mutex == nullptr) {
        buffers.mutex = xSemaphoreCreateMutex();
    }
    return buffers.mutex != nullptr;
}

bool ensurePcmBuffer() {
    auto& buffers = playBuffers();
    if (buffers.pcm == nullptr) {
        buffers.pcm = static_cast<uint8_t*>(
            heap_caps_malloc(kDecodedPcmMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return buffers.pcm != nullptr;
}

bool ensureJsonBodyBuffer() {
    auto& buffers = playBuffers();
    if (buffers.jsonBody == nullptr) {
        buffers.jsonBody = static_cast<char*>(
            heap_caps_malloc(kJsonBodyMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return buffers.jsonBody != nullptr;
}

bool lockPlayBuffers(TickType_t timeoutTicks) {
    return ensureMutex() && xSemaphoreTake(playBuffers().mutex, timeoutTicks) == pdTRUE;
}

void unlockPlayBuffers() {
    if (playBuffers().mutex != nullptr) {
        xSemaphoreGive(playBuffers().mutex);
    }
}

bool readTextBody(httpd_req_t* req, char* buffer, size_t bufferLen, size_t& outLen) {
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

bool readBinaryBody(httpd_req_t* req, uint8_t* buffer, size_t bufferLen, size_t& outLen) {
    outLen = 0;
    if (req == nullptr || buffer == nullptr || bufferLen == 0) {
        return false;
    }

    const int total = req->content_len;
    if (total <= 0 || static_cast<size_t>(total) > bufferLen) {
        return false;
    }

    int received = 0;
    while (received < total) {
        const int chunk = httpd_req_recv(req, reinterpret_cast<char*>(buffer + received), total - received);
        if (chunk <= 0) {
            return false;
        }
        received += chunk;
    }
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

esp_err_t sendOk(
    httpd_req_t* req,
    const char* requestId,
    const char* commandId,
    size_t ringUsedBytes,
    size_t ringFreeBytes,
    size_t ringCapacityBytes) {
    char body[384];
    snprintf(
        body,
        sizeof(body),
        "{\"v\":1,\"ok\":true,\"request_id\":\"%s\",\"command_id\":\"%s\","
        "\"ring_used_bytes\":%u,\"ring_free_bytes\":%u,\"ring_capacity_bytes\":%u}",
        requestId != nullptr ? requestId : "",
        commandId != nullptr ? commandId : "",
        static_cast<unsigned>(ringUsedBytes),
        static_cast<unsigned>(ringFreeBytes),
        static_cast<unsigned>(ringCapacityBytes));
    return sendJsonResponse(req, 200, body);
}

void copyJsonString(const cJSON* node, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return;
    }
    out[0] = '\0';
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
        strncpy(out, node->valuestring, outLen - 1);
        out[outLen - 1] = '\0';
    }
}

bool headerEquals(httpd_req_t* req, const char* name, const char* value) {
    char buffer[32] = {};
    if (httpd_req_get_hdr_value_str(req, name, buffer, sizeof(buffer)) != ESP_OK) {
        return false;
    }
    return strcmp(buffer, value) == 0;
}

uint16_t headerUint16(httpd_req_t* req, const char* name, uint16_t defaultValue) {
    char buffer[16] = {};
    if (httpd_req_get_hdr_value_str(req, name, buffer, sizeof(buffer)) != ESP_OK) {
        return defaultValue;
    }
    return static_cast<uint16_t>(atoi(buffer));
}

uint8_t headerUint8(httpd_req_t* req, const char* name, uint8_t defaultValue) {
    char buffer[16] = {};
    if (httpd_req_get_hdr_value_str(req, name, buffer, sizeof(buffer)) != ESP_OK) {
        return defaultValue;
    }
    return static_cast<uint8_t>(atoi(buffer));
}

bool headerBool(httpd_req_t* req, const char* name) {
    return headerEquals(req, name, "1") || headerEquals(req, name, "true");
}

bool copyHeader(httpd_req_t* req, const char* name, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return false;
    }
    out[0] = '\0';
    if (httpd_req_get_hdr_value_str(req, name, out, outLen) != ESP_OK) {
        return false;
    }
    return out[0] != '\0';
}

bool enqueuePcm(
    ApiContext* context,
    int16_t* samples,
    size_t sampleCount,
    uint16_t sampleRateHz,
    uint8_t channels,
    bool streamEnd) {
    if (context == nullptr || context->audioPlayback == nullptr) {
        return false;
    }
    if (sampleCount == 0) {
        if (streamEnd) {
            context->audioPlayback->endStream();
            return true;
        }
        return false;
    }
    return context->audioPlayback->enqueueDecodedPcm(
        samples, sampleCount, sampleRateHz, channels, streamEnd);
}

esp_err_t handleBinaryPlay(httpd_req_t* req, ApiContext* context, const int64_t t0Us) {
    char requestId[64] = {};
    char commandId[64] = {};
    copyHeader(req, "X-Request-Id", requestId, sizeof(requestId));
    copyHeader(req, "X-Command-Id", commandId, sizeof(commandId));

    if (!requireAuth(req, context, requestId)) {
        return ESP_OK;
    }

    char targetUid[64] = {};
    if (!copyHeader(req, "X-Target-Device-Uid", targetUid, sizeof(targetUid)) ||
        context->deviceUid == nullptr || strcmp(targetUid, context->deviceUid) != 0) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "target_device_uid mismatch", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    if (context->audioPlayback == nullptr || !context->audioPlayback->isRunning()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback service not running", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    if (!ensurePcmBuffer()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Out of memory", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    size_t bodyLen = 0;
    if (!readBinaryBody(req, playBuffers().pcm, kDecodedPcmMax, bodyLen)) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Request body too large or unreadable", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    const uint16_t sampleRateHz = headerUint16(req, "X-Sample-Rate", 16000);
    const uint8_t channels = headerUint8(req, "X-Channels", 1);
    const bool streamEnd = headerBool(req, "X-Stream-End");

    if (bodyLen > 0 && (bodyLen < sizeof(int16_t) || (bodyLen % sizeof(int16_t)) != 0)) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Invalid PCM payload", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }
    if (bodyLen == 0 && !streamEnd) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Empty PCM requires X-Stream-End", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    if (!lockPlayBuffers(pdMS_TO_TICKS(100))) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback handler busy", true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    const size_t sampleCount = bodyLen / sizeof(int16_t);
    const bool queued = enqueuePcm(
        context,
        reinterpret_cast<int16_t*>(playBuffers().pcm),
        sampleCount,
        sampleRateHz,
        channels,
        streamEnd);
    const size_t ringUsed = context->audioPlayback->ringUsedBytes();
    const size_t ringFree = context->audioPlayback->ringFreeBytes();
    const size_t ringCapacity = context->audioPlayback->ringCapacityBytes();
    unlockPlayBuffers();

    if (!queued) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback queue full", true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    const int64_t elapsedMs = (esp_timer_get_time() - t0Us) / 1000;
    if (elapsedMs > 80) {
        ESP_LOGW(kTag, "play chunk slow %lld ms (pcm=%u)", static_cast<long long>(elapsedMs),
                 static_cast<unsigned>(bodyLen));
    }

    return sendOk(req, requestId, commandId, ringUsed, ringFree, ringCapacity);
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
    const int64_t t0Us = esp_timer_get_time();
    auto* context = static_cast<ApiContext*>(req->user_ctx);

    char contentType[64] = {};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", contentType, sizeof(contentType)) == ESP_OK &&
        strstr(contentType, "application/octet-stream") != nullptr) {
        return handleBinaryPlay(req, context, t0Us);
    }

    if (!lockPlayBuffers(pdMS_TO_TICKS(100))) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback handler busy", true, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    if (!ensureJsonBodyBuffer() || !ensurePcmBuffer()) {
        unlockPlayBuffers();
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Out of memory", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    char* body = playBuffers().jsonBody;
    uint8_t* decoded = playBuffers().pcm;

    size_t bodyLen = 0;
    if (!readTextBody(req, body, kJsonBodyMax, bodyLen)) {
        unlockPlayBuffers();
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Request body too large or unreadable", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    cJSON* root = cJSON_Parse(body);
    if (root == nullptr) {
        unlockPlayBuffers();
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Malformed JSON", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    char requestId[64] = {};
    char commandId[64] = {};
    copyJsonString(cJSON_GetObjectItemCaseSensitive(root, "request_id"), requestId, sizeof(requestId));

    if (!requireAuth(req, context, requestId)) {
        cJSON_Delete(root);
        unlockPlayBuffers();
        return ESP_OK;
    }

    const cJSON* targetNode = cJSON_GetObjectItemCaseSensitive(root, "target_device_uid");
    if (!cJSON_IsString(targetNode) || context->deviceUid == nullptr ||
        strcmp(targetNode->valuestring, context->deviceUid) != 0) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "target_device_uid mismatch", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        unlockPlayBuffers();
        return sendJsonResponse(req, 400, errBody);
    }

    if (context->audioPlayback == nullptr || !context->audioPlayback->isRunning()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback service not running", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        unlockPlayBuffers();
        return sendJsonResponse(req, 503, errBody);
    }

    const cJSON* pcmNode = cJSON_GetObjectItemCaseSensitive(root, "pcm_b64");
    if (!cJSON_IsString(pcmNode) || pcmNode->valuestring == nullptr) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Missing pcm_b64", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        unlockPlayBuffers();
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

    copyJsonString(cJSON_GetObjectItemCaseSensitive(root, "command_id"), commandId, sizeof(commandId));

    bool streamEnd = false;
    const cJSON* streamEndNode = cJSON_GetObjectItemCaseSensitive(root, "stream_end");
    if (cJSON_IsBool(streamEndNode)) {
        streamEnd = cJSON_IsTrue(streamEndNode);
    }

    const size_t b64Len = strlen(pcmNode->valuestring);
    const size_t maxB64Len = ((kDecodedPcmMax + 2) / 3) * 4;
    if (b64Len > maxB64Len) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "pcm_b64 payload too large", false, requestId, errBody, sizeof(errBody));
        cJSON_Delete(root);
        unlockPlayBuffers();
        return sendJsonResponse(req, 400, errBody);
    }

    size_t decodedLen = 0;
    const int rc = mbedtls_base64_decode(decoded, kDecodedPcmMax, &decodedLen,
                                         reinterpret_cast<const unsigned char*>(pcmNode->valuestring), b64Len);
    cJSON_Delete(root);

    if (rc != 0 || (decodedLen > 0 && (decodedLen < sizeof(int16_t) || (decodedLen % sizeof(int16_t)) != 0))) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INVALID_REQUEST", "Invalid pcm_b64 payload", false, requestId, errBody, sizeof(errBody));
        unlockPlayBuffers();
        return sendJsonResponse(req, 400, errBody);
    }

    const size_t sampleCount = decodedLen / sizeof(int16_t);
    const bool queued = enqueuePcm(
        context,
        reinterpret_cast<int16_t*>(decoded),
        sampleCount,
        sampleRateHz,
        channels,
        streamEnd);
    const size_t ringUsed = context->audioPlayback->ringUsedBytes();
    const size_t ringFree = context->audioPlayback->ringFreeBytes();
    const size_t ringCapacity = context->audioPlayback->ringCapacityBytes();
    unlockPlayBuffers();

    if (!queued) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Playback queue full", true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    const int64_t elapsedMs = (esp_timer_get_time() - t0Us) / 1000;
    if (elapsedMs > 80) {
        ESP_LOGW(kTag, "play chunk slow %lld ms (pcm=%u)", static_cast<long long>(elapsedMs),
                 static_cast<unsigned>(decodedLen));
    }

    return sendOk(req, requestId, commandId, ringUsed, ringFree, ringCapacity);
}
