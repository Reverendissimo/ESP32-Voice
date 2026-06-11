        /**
         * @file alarm_evaluator.hpp
         * @brief Evaluates threshold crossings with hysteresis.
         *
         * Responsibilities:
 * - trigger/clear thresholds
 * - duration and cooldown gates
         *
         * Non-responsibilities:
 * - sensor driver access
 * - REST parsing
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Evaluates threshold crossings with hysteresis.
         */
        class AlarmEvaluator {
        public:
            /**
     * @brief Updates alarm state from sample.
     */
    bool evaluate();
        };
