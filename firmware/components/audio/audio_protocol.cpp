/**
 * @file audio_protocol.cpp
 * @brief Implementation of AudioProtocol.
 */
#include "audio_protocol.hpp"

#include <stdio.h>
#include <string.h>

#include "mbedtls/base64.h"

namespace {

bool appendBase64Pcm(const uint8_t* pcm, size_t pcmLen, char* outBuffer, size_t outBufferLen, size_t& used) {
    if (pcm == nullptr || outBuffer == nullptr || outBufferLen == 0) {
        return false;
    }

    size_t olen = 0;
    const int rc = mbedtls_base64_encode(
        reinterpret_cast<unsigned char*>(outBuffer + used),
        outBufferLen - used,
        &olen,
        pcm,
        pcmLen);
    if (rc != 0) {
        return false;
    }
    used += olen;
    if (used < outBufferLen) {
        outBuffer[used] = '\0';
    }
    return true;
}

}  // namespace

bool AudioProtocol::buildSpeechChunk(
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
    size_t outBufferLen) const {
    if (deviceUid == nullptr || utteranceId == nullptr || sessionId == nullptr || outBuffer == nullptr ||
        outBufferLen < 256 || pcm == nullptr || pcmLen == 0) {
        return false;
    }

    const char* name = (deviceName != nullptr && deviceName[0] != '\0') ? deviceName : "esp32-voice";

    size_t used = 0;
    const int headerLen = snprintf(
        outBuffer,
        outBufferLen,
        "{\"v\":1,\"protocol\":\"A2\",\"device_uid\":\"%s\",\"device_name\":\"%s\","
        "\"utterance_id\":\"%s\",\"session_id\":\"%s\",\"chunk_seq\":%lu,"
        "\"sample_rate_hz\":%u,\"channels\":%u,\"pcm_format\":\"s16le\",\"pcm_b64\":\"",
        deviceUid,
        name,
        utteranceId,
        sessionId,
        static_cast<unsigned long>(chunkSeq),
        static_cast<unsigned>(sampleRateHz),
        static_cast<unsigned>(channels));
    if (headerLen < 0 || static_cast<size_t>(headerLen) >= outBufferLen - 4) {
        return false;
    }
    used = static_cast<size_t>(headerLen);

    if (!appendBase64Pcm(pcm, pcmLen, outBuffer, outBufferLen, used)) {
        return false;
    }

    if (used + 2 >= outBufferLen) {
        return false;
    }
    outBuffer[used++] = '"';
    outBuffer[used++] = '}';
    outBuffer[used] = '\0';
    return true;
}

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
        "{\"v\":1,\"protocol\":\"A2\",\"device_uid\":\"%s\",\"device_name\":\"%s\","
        "\"utterance_id\":\"%s\",\"session_id\":\"%s\",\"chunk_count\":%lu,\"duration_ms\":%lu}",
        deviceUid,
        name,
        utteranceId,
        sessionId,
        static_cast<unsigned long>(chunkCount),
        static_cast<unsigned long>(durationMs));
    return written > 0 && static_cast<size_t>(written) < outBufferLen;
}
