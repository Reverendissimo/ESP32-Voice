/**
 * @file audio_protocol.hpp
 * @brief Protocol A2 JSON envelope builders for outbound speech traffic.
 *
 * Protocol A2 (device -> server):
 * - Chunk POST: JSON metadata + base64 PCM (s16le mono).
 * - Finalize POST: JSON metadata only after conservative silence timeout.
 *
 * Wake model B: always-on VAD gates upload (no wake-word).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Builds protocol A2 outbound JSON payloads.
 */
class AudioProtocol {
public:
    /**
     * @brief Builds a speech chunk JSON body.
     *
     * @return true when the payload fit in outBuffer.
     */
    bool buildSpeechChunk(
        const char* deviceUid,
        const char* deviceName,
        const char* utteranceId,
        const char* sessionId,
        uint32_t chunkSeq,
        uint16_t sampleRateHz,
        uint8_t channels,
        const uint8_t* pcm,
        size_t pcmLen,
        char* outBuffer,
        size_t outBufferLen) const;

    /**
     * @brief Builds a speech finalize JSON body.
     */
    bool buildSpeechFinalize(
        const char* deviceUid,
        const char* deviceName,
        const char* utteranceId,
        const char* sessionId,
        uint32_t chunkCount,
        uint32_t durationMs,
        char* outBuffer,
        size_t outBufferLen) const;
};
