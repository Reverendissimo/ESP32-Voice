        /**
         * @file audio_upload_service.hpp
         * @brief Streams utterance audio to backend server.
         *
         * Responsibilities:
 * - chunked upload during speech
         *
         * Non-responsibilities:
 * - VAD detection
 * - playback scheduling
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Streams utterance audio to backend server.
         */
        class AudioUploadService {
        public:
            /**
     * @brief Begins streaming current utterance.
     */
    bool startUtterance();
        };
