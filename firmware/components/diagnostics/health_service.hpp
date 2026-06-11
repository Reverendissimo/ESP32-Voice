        /**
         * @file health_service.hpp
         * @brief Builds health and status snapshots.
         *
         * Responsibilities:
 * - uptime, heap, wifi, dirty flag, time trust
         *
         * Non-responsibilities:
 * - HTTP server lifecycle
 * - sensor drivers
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Builds health and status snapshots.
         */
        class HealthService {
        public:
            /**
     * @brief Collects current health snapshot.
     */
    bool collect();
        };
