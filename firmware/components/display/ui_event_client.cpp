/**
 * @file ui_event_client.cpp
 * @brief Implementation of UiEventClient.
 */
#include "ui_event_client.hpp"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* kTag = "ui_event";

namespace {

bool isUrlConfigured(const char* url) {
    return url != nullptr && url[0] != '\0';
}

}  // namespace

void UiEventClient::configure(
    const config::CallbacksConfig& callbacks,
    const char* deviceUid,
    const char* deviceName,
    const char* authToken) {
    m_callbacks = callbacks;
    strncpy(m_deviceUid, deviceUid != nullptr ? deviceUid : "", sizeof(m_deviceUid) - 1);
    strncpy(m_deviceName, deviceName != nullptr ? deviceName : "", sizeof(m_deviceName) - 1);
    strncpy(m_authToken, authToken != nullptr ? authToken : "", sizeof(m_authToken) - 1);
    m_deviceUid[sizeof(m_deviceUid) - 1] = '\0';
    m_deviceName[sizeof(m_deviceName) - 1] = '\0';
    m_authToken[sizeof(m_authToken) - 1] = '\0';
}

bool UiEventClient::start() {
    if (m_running) {
        return true;
    }

    m_queue = xQueueCreate(8, sizeof(UiEventJob));
    if (m_queue == nullptr) {
        return false;
    }

    m_running = true;
    const BaseType_t created = xTaskCreate(
        workerTask,
        "ui_event",
        6144,
        this,
        3,
        reinterpret_cast<TaskHandle_t*>(&m_taskHandle));
    if (created != pdPASS) {
        m_running = false;
        vQueueDelete(static_cast<QueueHandle_t>(m_queue));
        m_queue = nullptr;
        return false;
    }
    return true;
}

void UiEventClient::stop() {
    m_running = false;
    if (m_taskHandle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (m_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(m_queue));
        m_queue = nullptr;
    }
    m_taskHandle = nullptr;
}

bool UiEventClient::isConfigured() const {
    return isUrlConfigured(m_callbacks.uiEventUrl);
}

bool UiEventClient::enqueueButtonPress(const char* componentId, const char* actionId, const char* requestId) {
    if (!m_running || m_queue == nullptr || !isConfigured()) {
        return false;
    }

    UiEventJob job = {};
    if (componentId != nullptr) {
        strncpy(job.componentId, componentId, sizeof(job.componentId) - 1);
    }
    if (actionId != nullptr) {
        strncpy(job.actionId, actionId, sizeof(job.actionId) - 1);
    }
    if (requestId != nullptr) {
        strncpy(job.requestId, requestId, sizeof(job.requestId) - 1);
    }

    return xQueueSend(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(100)) == pdTRUE;
}

void UiEventClient::workerTask(void* arg) {
    auto* self = static_cast<UiEventClient*>(arg);
    if (self != nullptr) {
        self->runWorker();
    }
    vTaskDelete(nullptr);
}

void UiEventClient::runWorker() {
    UiEventJob job = {};
    while (m_running) {
        if (!takeJob(job)) {
            continue;
        }
        if (!postEvent(job)) {
            ESP_LOGW(kTag, "ui event post failed action=%s", job.actionId);
        }
    }
}

bool UiEventClient::takeJob(UiEventJob& job) {
    if (m_queue == nullptr) {
        return false;
    }
    return xQueueReceive(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(200)) == pdTRUE;
}

bool UiEventClient::postEvent(const UiEventJob& job) const {
    if (!isConfigured()) {
        return false;
    }

    char body[512];
    const int written = snprintf(
        body,
        sizeof(body),
        "{\"v\":1,\"device_uid\":\"%s\",\"device_name\":\"%s\",\"event_type\":\"button_press\","
        "\"component_id\":\"%s\",\"action_id\":\"%s\",\"request_id\":\"%s\"}",
        m_deviceUid,
        m_deviceName,
        job.componentId,
        job.actionId,
        job.requestId);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(body)) {
        return false;
    }

    esp_http_client_config_t httpConfig = {};
    httpConfig.url = m_callbacks.uiEventUrl;
    httpConfig.method = HTTP_METHOD_POST;
    httpConfig.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (m_authToken[0] != '\0') {
        char authHeader[96];
        snprintf(authHeader, sizeof(authHeader), "Bearer %s", m_authToken);
        esp_http_client_set_header(client, "Authorization", authHeader);
    }

    esp_http_client_set_post_field(client, body, written);
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(kTag, "POST %s status=%d err=%s", m_callbacks.uiEventUrl, status, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag, "posted button action=%s component=%s", job.actionId, job.componentId);
    return true;
}
