/**
 * @file config_route.cpp
 * @brief Implementation of ConfigRoute.
 */
#include "config_route.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth_context.hpp"
#include "config_manager.hpp"
#include "error_response_factory.hpp"
#include "http_response_helpers.hpp"
#include "json_response_builder.hpp"
#include "wifi_manager.hpp"

static constexpr size_t kResponseBufferSize = 3072;
static constexpr size_t kRequestBodyMax = 2048;

namespace {

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

bool requireAuth(httpd_req_t* req, const ApiContext* context, char* requestId, size_t requestIdLen) {
    char token[64] = {};
    extractAuthToken(req, context, token, sizeof(token));
    if (context == nullptr || context->authContext == nullptr) {
        return false;
    }
    if (!context->authContext->authorize(token)) {
        ErrorResponseFactory errors;
        char body[256] = {};
        errors.build("UNAUTHORIZED", "Missing or invalid auth token", false, requestId, body, sizeof(body));
        sendJsonResponse(req, 401, body);
        return false;
    }
    return true;
}

esp_err_t sendConfig(httpd_req_t* req, const ApiContext* context, const config::AppConfig& config, bool dirty) {
    char* body = static_cast<char*>(malloc(kResponseBufferSize));
    if (body == nullptr) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Out of memory", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    JsonResponseBuilder builder;
    if (!builder.buildConfig(context->deviceUid, config, dirty, true, body, kResponseBufferSize)) {
        free(body);
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Failed to build config response", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    const esp_err_t result = sendJsonResponse(req, 200, body);
    free(body);
    return result;
}

esp_err_t sendOk(httpd_req_t* req, const char* requestId, const char* message) {
    char body[256];
    snprintf(
        body,
        sizeof(body),
        "{\"v\":1,\"ok\":true,\"message\":\"%s\",\"request_id\":\"%s\"}",
        message,
        requestId != nullptr ? requestId : "");
    return sendJsonResponse(req, 200, body);
}

bool applyLiveWifiIfPresent(const ApiContext* context) {
    if (context == nullptr || context->configManager == nullptr || context->wifiManager == nullptr) {
        return true;
    }
    const config::WifiConfig& wifi = context->configManager->active().wifi;
    if (wifi.ssid[0] == '\0') {
        return true;
    }
    return context->wifiManager->applyConfig(wifi);
}

}  // namespace

bool ConfigRoute::registerRoutes(httpd_handle_t server, const ApiContext* context) const {
    if (server == nullptr || context == nullptr) {
        return false;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/api/v1/config", .method = HTTP_GET, .handler = handleGetActive, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/saved", .method = HTTP_GET, .handler = handleGetSaved, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/patch", .method = HTTP_POST, .handler = handlePatch, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/apply", .method = HTTP_POST, .handler = handleApply, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/save", .method = HTTP_POST, .handler = handleSave, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/load", .method = HTTP_POST, .handler = handleLoad, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/revert", .method = HTTP_POST, .handler = handleRevert, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/reset", .method = HTTP_POST, .handler = handleReset, .user_ctx = const_cast<ApiContext*>(context)},
        {.uri = "/api/v1/config/reset_saved", .method = HTTP_POST, .handler = handleResetSaved, .user_ctx = const_cast<ApiContext*>(context)},
    };

    for (const httpd_uri_t& route : routes) {
        if (httpd_register_uri_handler(server, &route) != ESP_OK) {
            return false;
        }
    }
    return true;
}

esp_err_t ConfigRoute::handleGetActive(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    if (context == nullptr || context->configManager == nullptr) {
        return ESP_FAIL;
    }
    return sendConfig(req, context, context->configManager->active(), context->configManager->isDirty());
}

esp_err_t ConfigRoute::handleGetSaved(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    if (context == nullptr || context->configManager == nullptr) {
        return ESP_FAIL;
    }
    return sendConfig(req, context, context->configManager->saved(), false);
}

namespace {

bool patchFromRequest(httpd_req_t* req, ApiContext* context, char* requestId, size_t requestIdLen, char* validationError, size_t validationErrorLen) {
    char* body = nullptr;
    size_t bodyLen = 0;

    if (!readRequestBody(req, &body, &bodyLen) || bodyLen > kRequestBodyMax) {
        free(body);
        return false;
    }

    extractRequestId(body, requestId, requestIdLen);
    if (!requireAuth(req, context, requestId, requestIdLen)) {
        free(body);
        return false;
    }

    const bool ok = context->configManager->applyPatchJson(body, validationError, validationErrorLen);
    free(body);
    return ok;
}

}  // namespace

esp_err_t ConfigRoute::handlePatch(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char validationError[128] = {};

    if (context == nullptr || context->configManager == nullptr) {
        return ESP_FAIL;
    }

    if (!patchFromRequest(req, context, requestId, sizeof(requestId), validationError, sizeof(validationError))) {
        if (validationError[0] == '\0' && requestId[0] == '\0') {
            ErrorResponseFactory errors;
            char errBody[256] = {};
            errors.build("INVALID_REQUEST", "Invalid request body", false, requestId, errBody, sizeof(errBody));
            return sendJsonResponse(req, 400, errBody);
        }
        if (validationError[0] != '\0') {
            ErrorResponseFactory errors;
            char errBody[256] = {};
            errors.build("INVALID_REQUEST", validationError, false, requestId, errBody, sizeof(errBody));
            return sendJsonResponse(req, 400, errBody);
        }
        return ESP_OK;
    }

    if (context->reloadRuntimeConfig != nullptr) {
        context->reloadRuntimeConfig(context->runtimeReloadContext);
    }

    return sendOk(req, requestId, "patched");
}

esp_err_t ConfigRoute::handleApply(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char validationError[128] = {};

    if (context == nullptr || context->configManager == nullptr) {
        return ESP_FAIL;
    }

    if (!patchFromRequest(req, context, requestId, sizeof(requestId), validationError, sizeof(validationError))) {
        if (validationError[0] != '\0') {
            ErrorResponseFactory errors;
            char errBody[256] = {};
            errors.build("INVALID_REQUEST", validationError, false, requestId, errBody, sizeof(errBody));
            return sendJsonResponse(req, 400, errBody);
        }
        return ESP_OK;
    }

    if (!applyLiveWifiIfPresent(context)) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("APPLY_FAILED", "Config patched but live Wi-Fi apply failed", true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    if (context->reloadRuntimeConfig != nullptr) {
        context->reloadRuntimeConfig(context->runtimeReloadContext);
    }

    return sendOk(req, requestId, "applied");
}

esp_err_t ConfigRoute::handleSave(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char* body = nullptr;
    size_t bodyLen = 0;
    readRequestBody(req, &body, &bodyLen);
    extractRequestId(body, requestId, sizeof(requestId));
    free(body);

    if (!requireAuth(req, context, requestId, sizeof(requestId))) {
        return ESP_OK;
    }

    if (!context->configManager->saveActive()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("SAVE_FAILED", "Failed to save active config", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    if (context->reloadRuntimeConfig != nullptr) {
        context->reloadRuntimeConfig(context->runtimeReloadContext);
    }

    return sendOk(req, requestId, "saved");
}

esp_err_t ConfigRoute::handleLoad(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char* body = nullptr;
    size_t bodyLen = 0;
    readRequestBody(req, &body, &bodyLen);
    extractRequestId(body, requestId, sizeof(requestId));
    free(body);

    if (!requireAuth(req, context, requestId, sizeof(requestId))) {
        return ESP_OK;
    }

    if (!context->configManager->loadSavedIntoActive()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("LOAD_FAILED", "No valid saved config to load", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 400, errBody);
    }

    if (context->reloadRuntimeConfig != nullptr) {
        context->reloadRuntimeConfig(context->runtimeReloadContext);
    }

    return sendConfig(req, context, context->configManager->active(), context->configManager->isDirty());
}

esp_err_t ConfigRoute::handleRevert(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char* body = nullptr;
    size_t bodyLen = 0;
    readRequestBody(req, &body, &bodyLen);
    extractRequestId(body, requestId, sizeof(requestId));
    free(body);

    if (!requireAuth(req, context, requestId, sizeof(requestId))) {
        return ESP_OK;
    }

    context->configManager->revertActive();
    if (context->reloadRuntimeConfig != nullptr) {
        context->reloadRuntimeConfig(context->runtimeReloadContext);
    }
    return sendConfig(req, context, context->configManager->active(), context->configManager->isDirty());
}

esp_err_t ConfigRoute::handleReset(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char* body = nullptr;
    size_t bodyLen = 0;
    readRequestBody(req, &body, &bodyLen);
    extractRequestId(body, requestId, sizeof(requestId));
    free(body);

    if (!requireAuth(req, context, requestId, sizeof(requestId))) {
        return ESP_OK;
    }

    context->configManager->resetActiveToDefaults();
    return sendConfig(req, context, context->configManager->active(), context->configManager->isDirty());
}

esp_err_t ConfigRoute::handleResetSaved(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char requestId[64] = {};
    char* body = nullptr;
    size_t bodyLen = 0;
    readRequestBody(req, &body, &bodyLen);
    extractRequestId(body, requestId, sizeof(requestId));
    free(body);

    if (!requireAuth(req, context, requestId, sizeof(requestId))) {
        return ESP_OK;
    }

    if (!context->configManager->resetSaved()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("RESET_FAILED", "Failed to reset saved config", false, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    return sendConfig(req, context, context->configManager->active(), context->configManager->isDirty());
}
