/**
 * @file time_sync_service.cpp
 * @brief Implementation of TimeSyncService.
 */
#include "time_sync_service.hpp"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* kTag = "time_sync";

namespace {

void onTimeSync(struct timeval* tv) {
    (void)tv;
    ESP_LOGI(kTag, "SNTP time synchronized");
}

}  // namespace

bool TimeSyncService::start(const config::TimeConfig& timeConfig) {
    if (m_started) {
        return true;
    }

    setenv("TZ", timeConfig.timezone, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, timeConfig.sntpServer);
    esp_sntp_set_time_sync_notification_cb(onTimeSync);
    esp_sntp_set_sync_interval(timeConfig.syncIntervalSec * 1000U);
    esp_sntp_init();

    m_started = true;
    ESP_LOGI(kTag, "SNTP started server=%s", timeConfig.sntpServer);
    return true;
}

bool TimeSyncService::syncNow() {
    if (!m_started) {
        return false;
    }

    esp_sntp_restart();

    for (int attempt = 0; attempt < 20; ++attempt) {
        const time_t now = time(nullptr);
        if (now > 1700000000) {
            m_timeTrusted = true;
            m_lastSyncUnix = now;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    m_timeTrusted = false;
    return false;
}

bool TimeSyncService::isTimeTrusted() const {
    if (!m_started) {
        return false;
    }
    return time(nullptr) > 1700000000;
}

int64_t TimeSyncService::lastSyncUnix() const {
    return m_lastSyncUnix;
}
