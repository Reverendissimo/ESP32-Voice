/**
 * @file utterance_state_machine.cpp
 * @brief Implementation of UtteranceStateMachine.
 */
#include "utterance_state_machine.hpp"

#include <stdio.h>
#include <string.h>

#include "audio_upload_service.hpp"
#include "esp_log.h"

static const char* kTag = "utterance_fsm";

void UtteranceStateMachine::configure(AudioUploadService* uploadService, const char* sessionId) {
    m_uploadService = uploadService;
    m_state = UtteranceState::Idle;
    m_chunkCount = 0;
    m_durationMs = 0;
    m_utteranceCounter = 0;
    m_utteranceId[0] = '\0';
    if (sessionId != nullptr) {
        strncpy(m_sessionId, sessionId, sizeof(m_sessionId) - 1);
        m_sessionId[sizeof(m_sessionId) - 1] = '\0';
    } else {
        m_sessionId[0] = '\0';
    }
}

bool UtteranceStateMachine::beginUtterance() {
    if (m_uploadService == nullptr) {
        return false;
    }

    ++m_utteranceCounter;
    snprintf(
        m_utteranceId,
        sizeof(m_utteranceId),
        "utt_%lu",
        static_cast<unsigned long>(m_utteranceCounter));

    m_chunkCount = 0;
    m_durationMs = 0;

    if (!m_uploadService->startUtterance(m_utteranceId, m_sessionId)) {
        ESP_LOGW(kTag, "upload start failed for %s", m_utteranceId);
        m_utteranceId[0] = '\0';
        return false;
    }

    m_state = UtteranceState::Streaming;
    ESP_LOGI(kTag, "utterance start id=%s", m_utteranceId);
    return true;
}

bool UtteranceStateMachine::endUtterance() {
    if (m_state != UtteranceState::Streaming || m_uploadService == nullptr) {
        m_state = UtteranceState::Idle;
        return false;
    }

    const bool ok = m_uploadService->finalizeUtterance(m_utteranceId, m_sessionId, m_chunkCount, m_durationMs);
    ESP_LOGI(
        kTag,
        "utterance end id=%s chunks=%lu duration_ms=%lu ok=%d",
        m_utteranceId,
        static_cast<unsigned long>(m_chunkCount),
        static_cast<unsigned long>(m_durationMs),
        ok);

    m_state = UtteranceState::Idle;
    m_utteranceId[0] = '\0';
    return ok;
}

bool UtteranceStateMachine::handleVadEvent(audio::VadEventType event, uint16_t frameDurationMs) {
    switch (event) {
        case audio::VadEventType::SpeechStart:
            if (m_state == UtteranceState::Idle) {
                return beginUtterance();
            }
            return true;
        case audio::VadEventType::SpeechActive:
            if (m_state == UtteranceState::Streaming) {
                m_durationMs += frameDurationMs;
            }
            return true;
        case audio::VadEventType::SpeechEnd:
            if (m_state == UtteranceState::Streaming) {
                m_durationMs += frameDurationMs;
                return endUtterance();
            }
            return true;
        case audio::VadEventType::Silence:
        default:
            return true;
    }
}

void UtteranceStateMachine::onChunkUploaded() {
    if (m_state == UtteranceState::Streaming) {
        ++m_chunkCount;
    }
}

bool UtteranceStateMachine::isStreaming() const {
    return m_state == UtteranceState::Streaming;
}

UtteranceState UtteranceStateMachine::state() const {
    return m_state;
}

const char* UtteranceStateMachine::utteranceId() const {
    return m_utteranceId;
}

const char* UtteranceStateMachine::sessionId() const {
    return m_sessionId;
}

uint32_t UtteranceStateMachine::chunkCount() const {
    return m_chunkCount;
}

uint32_t UtteranceStateMachine::durationMs() const {
    return m_durationMs;
}
