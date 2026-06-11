/**
 * @file http_server_service.hpp
 * @brief Owns local HTTP server lifecycle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "api_context.hpp"
#include "esp_http_server.h"

/**
 * @brief Local REST HTTP server wrapper.
 */
class HttpServerService {
public:
    /**
     * @brief Starts HTTP server and registers routes.
     */
    bool start(uint16_t port, const ApiContext* context);

    /**
     * @brief Stops HTTP server if running.
     */
    void stop();

    /**
     * @brief Returns true when server handle is active.
     */
    bool isRunning() const;

    /**
     * @brief Returns server handle for advanced registration.
     */
    httpd_handle_t handle() const;

private:
    httpd_handle_t m_server = nullptr;
};
