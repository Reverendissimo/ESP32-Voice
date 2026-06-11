        /**
         * @file utterance_state_machine.hpp
         * @brief Tracks utterance lifecycle from VAD events.
         *
         * Responsibilities:
 * - conservative finalize after silence
         *
         * Non-responsibilities:
 * - HTTP client details
 * - microphone driver
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Tracks utterance lifecycle from VAD events.
         */
        class UtteranceStateMachine {
        public:
            /**
     * @brief Handles speech start event.
     */
    bool onSpeechStart();
        };
