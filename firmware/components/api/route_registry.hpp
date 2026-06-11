/**
 * @file route_registry.hpp
 * @brief Registers REST route handlers with the HTTP server.
 */
#pragma once

#include <stdbool.h>

#include "api_context.hpp"
#include "esp_http_server.h"

/**
 * @brief Registers /api/v1 routes on the HTTP server.
 */
class RouteRegistry {
public:
    /**
     * @brief Registers implemented routes for step 3 and stubs for future routes.
     */
    bool registerAll(httpd_handle_t server, const ApiContext* context) const;
};
