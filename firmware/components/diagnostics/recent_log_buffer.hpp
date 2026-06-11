        /**
         * @file recent_log_buffer.hpp
         * @brief Retains a bounded ring of recent log lines.
         *
         * Responsibilities:
 * - append and retrieve recent logs
         *
         * Non-responsibilities:
 * - ESP-IDF log hook installation details in routes
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Retains a bounded ring of recent log lines.
         */
        class RecentLogBuffer {
        public:
            /**
     * @brief Appends one log line.
     */
    bool append();
        };
