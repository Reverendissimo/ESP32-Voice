/**
 * @file display_route.hpp
 * @brief Handles POST /api/v1/display.
 */
#pragma once

#include <stdbool.h>

#include "esp_http_server.h"

struct ApiContext;

/**
 * @brief Registers and handles the display command route.
 */
class DisplayRoute {
public:
    bool registerRoutes(httpd_handle_t server, const ApiContext* context) const;

private:
    static esp_err_t handlePost(httpd_req_t* req);
};
