        /**
         * @file radar_service.hpp
         * @brief Reads radar-based presence when enabled.
         *
         * Responsibilities:
 * - presence state updates
         *
         * Non-responsibilities:
 * - alarm evaluation
 * - HTTP handlers
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Reads radar-based presence when enabled.
         */
        class RadarService {
        public:
            /**
     * @brief Starts radar sampling.
     */
    bool start();
        };
