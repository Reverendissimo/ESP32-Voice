/**
 * @file version_route.cpp
 * @brief Implementation of VersionRoute.
 */
#include "version_route.hpp"

#include "error_response_factory.hpp"
#include "http_response_helpers.hpp"
#include "json_response_builder.hpp"

static constexpr size_t kResponseBufferSize = 512;

bool VersionRoute::registerRoutes(httpd_handle_t server, const ApiContext* context) const {
    if (server == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t versionUri = {
        .uri = "/api/v1/version",
        .method = HTTP_GET,
        .handler = handleGet,
        .user_ctx = const_cast<ApiContext*>(context),
    };
    httpd_uri_t apiVersionUri = {
        .uri = "/api/v1/api/version",
        .method = HTTP_GET,
        .handler = handleGet,
        .user_ctx = const_cast<ApiContext*>(context),
    };

    return httpd_register_uri_handler(server, &versionUri) == ESP_OK &&
           httpd_register_uri_handler(server, &apiVersionUri) == ESP_OK;
}

esp_err_t VersionRoute::handleGet(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);

    JsonResponseBuilder builder;
    char body[kResponseBufferSize] = {};
    if (!builder.buildVersion(context != nullptr ? context->deviceUid : "", body, sizeof(body))) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("INTERNAL_ERROR", "Failed to build version response", false, "", errBody, sizeof(errBody));
        return sendJsonResponse(req, 500, errBody);
    }

    return sendJsonResponse(req, 200, body);
}
