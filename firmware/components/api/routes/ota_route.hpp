/**
 * @file ota_route.hpp
 * @brief OTA status and update routes (enabled only when config.ota.secret is set).
 */
#pragma once

#include "esp_http_server.h"

struct ApiContext;

class OtaRoute {
public:
    bool registerRoutes(httpd_handle_t server, const ApiContext* context) const;

private:
    static esp_err_t handleGetStatus(httpd_req_t* req);
    static esp_err_t handlePostUpdate(httpd_req_t* req);
};
