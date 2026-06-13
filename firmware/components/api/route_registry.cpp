/**
 * @file route_registry.cpp
 * @brief Implementation of RouteRegistry.
 */
#include "route_registry.hpp"

#include "error_response_factory.hpp"
#include "http_response_helpers.hpp"
#include "routes/config_route.hpp"
#include "routes/health_route.hpp"
#include "routes/display_route.hpp"
#include "routes/play_route.hpp"
#include "routes/version_route.hpp"

namespace {

esp_err_t handleNotImplemented(httpd_req_t* req) {
    ErrorResponseFactory errors;
    char body[256] = {};
    errors.build("NOT_IMPLEMENTED", "Endpoint not implemented yet", false, "", body, sizeof(body));
    return sendJsonResponse(req, 501, body);
}

bool registerStub(httpd_handle_t server, const char* uri, httpd_method_t method) {
    httpd_uri_t route = {
        .uri = uri,
        .method = method,
        .handler = handleNotImplemented,
        .user_ctx = nullptr,
    };
    return httpd_register_uri_handler(server, &route) == ESP_OK;
}

}  // namespace

bool RouteRegistry::registerAll(httpd_handle_t server, const ApiContext* context) const {
    if (server == nullptr || context == nullptr) {
        return false;
    }

    HealthRoute healthRoute;
    VersionRoute versionRoute;
    ConfigRoute configRoute;
    PlayRoute playRoute;
    DisplayRoute displayRoute;

    if (!healthRoute.registerRoutes(server, context) || !versionRoute.registerRoutes(server, context) ||
        !configRoute.registerRoutes(server, context) || !playRoute.registerRoutes(server, context) ||
        !displayRoute.registerRoutes(server, context)) {
        return false;
    }

    const char* postStubs[] = {
        "/api/v1/time/sync",
        "/api/v1/wifi/test",
        "/api/v1/ir/learn/start",
        "/api/v1/ir/send",
    };
    const char* getStubs[] = {
        "/api/v1/metrics",
        "/api/v1/logs/recent",
        "/api/v1/time",
        "/api/v1/wifi/status",
        "/api/v1/wifi/scan",
        "/api/v1/battery",
        "/api/v1/power",
        "/api/v1/environment",
    };

    for (const char* uri : postStubs) {
        if (!registerStub(server, uri, HTTP_POST)) {
            return false;
        }
    }
    for (const char* uri : getStubs) {
        if (!registerStub(server, uri, HTTP_GET)) {
            return false;
        }
    }

    return true;
}
