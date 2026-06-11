        /**
         * @file vad_service.hpp
         * @brief Detects speech activity in captured audio.
         *
         * Responsibilities:
 * - speech start/stop events
         *
         * Non-responsibilities:
 * - upload transport
 * - playback
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Detects speech activity in captured audio.
         */
        class VadService {
        public:
            /**
     * @brief Starts VAD processing.
     */
    bool start();
        };
