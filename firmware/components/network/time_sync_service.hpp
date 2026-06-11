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

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Owns SNTP time synchronization state.
         */
        class TimeSyncService {
        public:
            /**
     * @brief Starts SNTP synchronization.
     */
    bool start();
        };
