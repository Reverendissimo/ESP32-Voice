/**
 * @file utterance_state_machine.hpp
 * @brief Tracks utterance lifecycle from VAD events.
 *
 * Responsibilities:
 * - conservative finalize after silence
 * - utterance/session id generation
 *
 * Non-responsibilities:
 * - HTTP client details
 * - microphone driver
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_types.hpp"

class AudioUploadService;

enum class UtteranceState : uint8_t {
    Idle,
    Streaming,
};

/**
 * @brief Tracks utterance lifecycle from VAD events.
 */
class UtteranceStateMachine {
public:
    /**
     * @brief Configures upload target and session id prefix.
     */
    void configure(AudioUploadService* uploadService, const char* sessionId);

    /**
     * @brief Handles a VAD event and drives upload start/finalize.
     */
    bool handleVadEvent(audio::VadEventType event, uint16_t frameDurationMs);

    /**
     * @brief Returns true while frames should be uploaded.
     */
    bool isStreaming() const;

    UtteranceState state() const;
    const char* utteranceId() const;
    const char* sessionId() const;
    uint32_t chunkCount() const;
    uint32_t durationMs() const;

    /**
     * @brief Called after a chunk POST succeeds.
     */
    void onChunkUploaded();

private:
    bool beginUtterance();
    bool endUtterance();

    AudioUploadService* m_uploadService = nullptr;
    UtteranceState m_state = UtteranceState::Idle;
    char m_sessionId[48];
    char m_utteranceId[48];
    uint32_t m_chunkCount;
    uint32_t m_durationMs;
    uint32_t m_utteranceCounter;
};
