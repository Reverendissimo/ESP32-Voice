/**
 * @file audio_protocol.cpp
 * @brief Implementation of AudioProtocol.
 */
#include "audio_protocol.hpp"

#include <stdio.h>
#include <string.h>

bool AudioProtocol::buildSpeechFinalize(
    const char* deviceUid,
    const char* deviceName,
    const char* utteranceId,
    const char* sessionId,
    uint32_t chunkCount,
    uint32_t durationMs,
    char* outBuffer,
    size_t outBufferLen) const {
    if (deviceUid == nullptr || utteranceId == nullptr || sessionId == nullptr || outBuffer == nullptr) {
        return false;
    }

    const char* name = (deviceName != nullptr && deviceName[0] != '\0') ? deviceName : "esp32-voice";

    const int written = snprintf(
        outBuffer,
        outBufferLen,
        "{\"v\":1,\"protocol\":\"A3\",\"device_uid\":\"%s\",\"device_name\":\"%s\","
        "\"utterance_id\":\"%s\",\"session_id\":\"%s\",\"chunk_count\":%lu,\"duration_ms\":%lu}",
        deviceUid,
        name,
        utteranceId,
        sessionId,
        static_cast<unsigned long>(chunkCount),
        static_cast<unsigned long>(durationMs));
    return written > 0 && static_cast<size_t>(written) < outBufferLen;
}
