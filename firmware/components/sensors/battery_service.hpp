        /**
         * @file battery_service.hpp
         * @brief Reads battery voltage, percent, and charging state.
         *
         * Responsibilities:
 * - battery telemetry
         *
         * Non-responsibilities:
 * - alarm evaluation
 * - HTTP handlers
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Reads battery voltage, percent, and charging state.
         */
        class BatteryService {
        public:
            /**
     * @brief Starts battery monitoring.
     */
    bool start();
        };
