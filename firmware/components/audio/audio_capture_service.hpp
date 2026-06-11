        /**
         * @file audio_capture_service.hpp
         * @brief Captures microphone audio frames.
         *
         * Responsibilities:
 * - start/stop capture pipeline
         *
         * Non-responsibilities:
 * - VAD decisions
 * - HTTP upload
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Captures microphone audio frames.
         */
        class AudioCaptureService {
        public:
            /**
     * @brief Starts audio capture.
     */
    bool start();
        };
