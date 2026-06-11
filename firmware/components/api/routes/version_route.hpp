/**
 * @file version_route.hpp
 * @brief Handles GET /api/v1/version and GET /api/v1/api/version.
 */
#pragma once

#include <stdbool.h>

#include "api_context.hpp"
#include "esp_http_server.h"

/**
 * @brief Firmware/API version route registration.
 */
class VersionRoute {
public:
    /**
     * @brief Registers version routes.
     */
    bool registerRoutes(httpd_handle_t server, const ApiContext* context) const;

private:
    static esp_err_t handleGet(httpd_req_t* req);
};
