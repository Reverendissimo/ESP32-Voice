        /**
         * @file retry_policy.hpp
         * @brief Computes bounded exponential retry delays.
         *
         * Responsibilities:
 * - backoff sequence and cap handling
         *
         * Non-responsibilities:
 * - network transport
 * - HTTP handlers
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Computes bounded exponential retry delays.
         */
        class RetryPolicy {
        public:
            /**
     * @brief Returns next retry delay in ms.
     */
    uint32_t nextDelayMs();
        };
