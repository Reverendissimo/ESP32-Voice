/**
 * @file ui_event_client.hpp
 * @brief Posts UI interaction events to the configured callback URL.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "config_models.hpp"
#include "screen_model.hpp"

/**
 * @brief Async outbound client for POST /ui-event style callbacks.
 */
class UiEventClient {
public:
    void configure(
        const config::CallbacksConfig& callbacks,
        const char* deviceUid,
        const char* deviceName,
        const char* authToken);

    bool start();
    void stop();
    bool isConfigured() const;

    bool enqueueButtonPress(const char* componentId, const char* actionId, const char* requestId);

private:
    struct UiEventJob {
        char componentId[display::kMaxIdLen];
        char actionId[display::kMaxIdLen];
        char requestId[48];
    };

    static void workerTask(void* arg);
    void runWorker();
    bool takeJob(UiEventJob& job);
    bool postEvent(const UiEventJob& job) const;

    config::CallbacksConfig m_callbacks = {};
    char m_deviceUid[32] = {};
    char m_deviceName[32] = {};
    char m_authToken[64] = {};
    bool m_running = false;
    void* m_queue = nullptr;
    void* m_taskHandle = nullptr;
};
