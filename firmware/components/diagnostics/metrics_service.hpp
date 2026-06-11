        /**
         * @file metrics_service.hpp
         * @brief Exposes runtime counters and gauges.
         *
         * Responsibilities:
 * - metrics aggregation for /metrics
         *
         * Non-responsibilities:
 * - health summary composition
 * - REST routing
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Exposes runtime counters and gauges.
         */
        class MetricsService {
        public:
            /**
     * @brief Collects metrics snapshot.
     */
    bool collect();
        };
