/**
 * @file time_sync_service.hpp
 * @brief Owns SNTP time synchronization state.
 *
 * Responsibilities:
 * - sync system time
 * - expose trusted/untrusted state
 *
 * Non-responsibilities:
 * - HTTP route parsing
 * - config storage
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_models.hpp"

/**
 * @brief SNTP-based time synchronization service.
 */
class TimeSyncService {
public:
    /**
     * @brief Starts SNTP using active time configuration.
     */
    bool start(const config::TimeConfig& timeConfig);

    /**
     * @brief Forces an immediate SNTP sync attempt.
     */
    bool syncNow();

    /**
     * @brief Returns true when system time is considered trusted.
     */
    bool isTimeTrusted() const;

    /**
     * @brief Unix timestamp of last successful sync, or 0.
     */
    int64_t lastSyncUnix() const;

private:
    bool m_started = false;
    bool m_timeTrusted = false;
    int64_t m_lastSyncUnix = 0;
};
