/**
 * @file http_server_service.cpp
 * @brief Implementation of HttpServerService.
 */
#include "http_server_service.hpp"

#include "esp_log.h"
#include "route_registry.hpp"

static const char* kTag = "http_server";

bool HttpServerService::start(uint16_t port, const ApiContext* context) {
    if (m_server != nullptr) {
        return true;
    }
    if (context == nullptr || port == 0) {
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 32;
    config.lru_purge_enable = true;

    if (httpd_start(&m_server, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server");
        m_server = nullptr;
        return false;
    }

    RouteRegistry registry;
    if (!registry.registerAll(m_server, context)) {
        ESP_LOGE(kTag, "Failed to register routes");
        stop();
        return false;
    }

    ESP_LOGI(kTag, "HTTP server listening on port %u", static_cast<unsigned>(port));
    return true;
}

void HttpServerService::stop() {
    if (m_server != nullptr) {
        httpd_stop(m_server);
        m_server = nullptr;
    }
}

bool HttpServerService::isRunning() const {
    return m_server != nullptr;
}

httpd_handle_t HttpServerService::handle() const {
    return m_server;
}
