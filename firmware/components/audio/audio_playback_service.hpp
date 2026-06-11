        /**
         * @file audio_playback_service.hpp
         * @brief Plays async audio commanded by server.
         *
         * Responsibilities:
 * - queue and play remote audio payloads
         *
         * Non-responsibilities:
 * - speech capture
 * - display rendering
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Plays async audio commanded by server.
         */
        class AudioPlaybackService {
        public:
            /**
     * @brief Queues playback payload.
     */
    bool play();
        };
