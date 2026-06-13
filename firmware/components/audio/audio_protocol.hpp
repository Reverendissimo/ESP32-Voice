/**
 * @file audio_protocol.hpp
 * @brief Protocol builders for outbound speech traffic.
 *
 * Protocol A3 (device -> server):
 * - Stream POST: binary PCM (s16le mono) via chunked HTTP body + metadata headers.
 * - Finalize POST: JSON metadata only after conservative silence timeout.
 *
 * Wake model B: always-on VAD gates upload (no wake-word).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Builds protocol outbound JSON payloads.
 */
class AudioProtocol {
public:
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
