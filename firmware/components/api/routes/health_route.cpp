/**
 * @file health_route.cpp
 * @brief Implementation of HealthRoute.
 */
#include "config_manager.hpp"
#include "time_sync_service.hpp"
#include "wifi_manager.hpp"

#include "health_route.hpp"
#include "error_response_factory.hpp"
#include "http_response_helpers.hpp"
#include "json_response_builder.hpp"

#include <stdlib.h>

static constexpr size_t kResponseBufferSize = 2048;

namespace {

bool collectSnapshot(const ApiContext* context, HealthSnapshot& snapshot) {
    if (context == nullptr || context->healthService == nullptr) {
        return false;
    }

    HealthInputs inputs = {};
    inputs.deviceUid = context->deviceUid;
    if (context->configManager != nullptr) {
        inputs.deviceName = context->configManager->active().identity.deviceName;
        inputs.configDirty = context->configManager->isDirty();
    }
    if (context->wifiManager != nullptr) {
        inputs.wifiState = context->wifiManager->stateLabel();
        inputs.rssi = context->wifiManager->rssi();
    }
    if (context->timeSyncService != nullptr) {
        inputs.timeTrusted = context->timeSyncService->isTimeTrusted();
    }

    return context->healthService->collect(inputs, snapshot);
}

esp_err_t sendHealth(httpd_req_t* req, const ApiContext* context) {
    HealthSnapshot snapshot = {};
    if (!collectSnapshot(context, snapshot)) {
        ErrorResponseFactory errors;
        char body[256] = {};
        errors.build("INTERNAL_ERROR", "Failed to collect health", false, "", body, sizeof(body));
        return sendJsonResponse(req, 500, body);
    }

    char* body = static_cast<char*>(malloc(kResponseBufferSize));
    if (body == nullptr) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Out of memory", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    JsonResponseBuilder builder;
    if (!builder.buildHealth(context->deviceUid, snapshot, body, kResponseBufferSize)) {
        free(body);
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Failed to build health response", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    const esp_err_t result = sendJsonResponse(req, 200, body);
    free(body);
    return result;
}

}  // namespace

bool HealthRoute::registerRoutes(httpd_handle_t server, const ApiContext* context) const {
    if (server == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t healthUri = {
        .uri = "/api/v1/health",
        .method = HTTP_GET,
        .handler = handleGet,
        .user_ctx = const_cast<ApiContext*>(context),
    };
    httpd_uri_t statusUri = {
        .uri = "/api/v1/status",
        .method = HTTP_GET,
        .handler = handleGet,
        .user_ctx = const_cast<ApiContext*>(context),
    };

    return httpd_register_uri_handler(server, &healthUri) == ESP_OK &&
           httpd_register_uri_handler(server, &statusUri) == ESP_OK;
}

esp_err_t HealthRoute::handleGet(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    return sendHealth(req, context);
}
