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
#include "config_models.hpp"

class AudioUploadService;

enum class UtteranceState : uint8_t {
    Idle,
    Streaming,
    PostRoll,
};

/**
 * @brief Tracks utterance lifecycle from VAD events.
 */
class UtteranceStateMachine {
public:
    /**
     * @brief Configures upload target, session id, and utterance padding.
     */
    void configure(AudioUploadService* uploadService, const char* sessionId, const config::VadConfig& vad);

    /**
     * @brief Handles a VAD event and drives upload start/finalize.
     */
    bool handleVadEvent(audio::VadEventType event, uint16_t frameDurationMs);

    /**
     * @brief Returns true while frames should be uploaded (speech + padding).
     */
    bool shouldUpload() const;

    /**
     * @brief Per-frame tick for post-roll countdown.
     */
    void onFrameTick(uint16_t frameDurationMs);

    /**
     * @brief Returns true while an utterance is open (streaming or post-roll).
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

    /**
     * @brief Finalizes and clears any in-progress utterance (e.g. mic muted).
     */
    void cancelActive();

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
    uint32_t m_postRollRemainingMs = 0;
    uint16_t m_postRollPaddingMs = 1000;
};
