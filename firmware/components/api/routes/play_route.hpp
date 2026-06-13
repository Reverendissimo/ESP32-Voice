/**
 * @file play_route.hpp
 * @brief Handles POST /api/v1/play.
 */
#pragma once

#include <stdbool.h>

#include "esp_http_server.h"

struct ApiContext;

/**
 * @brief Handles POST /api/v1/play.
 */
class PlayRoute {
public:
    /**
     * @brief Registers POST /api/v1/play.
     */
    bool registerRoutes(httpd_handle_t server, const ApiContext* context) const;

private:
    static esp_err_t handlePost(httpd_req_t* req);
};
