/**
 * @file config_route.hpp
 * @brief Handles /api/v1/config routes.
 */
#pragma once

#include <stdbool.h>

#include "api_context.hpp"
#include "esp_http_server.h"

/**
 * @brief Configuration REST route registration.
 */
class ConfigRoute {
public:
    /**
     * @brief Registers config routes.
     */
    bool registerRoutes(httpd_handle_t server, const ApiContext* context) const;

private:
    static esp_err_t handleGetActive(httpd_req_t* req);
    static esp_err_t handleGetSaved(httpd_req_t* req);
    static esp_err_t handlePatch(httpd_req_t* req);
    static esp_err_t handleApply(httpd_req_t* req);
    static esp_err_t handleSave(httpd_req_t* req);
    static esp_err_t handleLoad(httpd_req_t* req);
    static esp_err_t handleRevert(httpd_req_t* req);
    static esp_err_t handleReset(httpd_req_t* req);
    static esp_err_t handleResetSaved(httpd_req_t* req);
};
