/**
 * @file audio_types.hpp
 * @brief Shared audio constants and frame structures.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace audio {

constexpr uint16_t kDefaultSampleRateHz = 16000;
constexpr uint8_t kDefaultChannels = 1;
constexpr uint16_t kFrameDurationMs = 20;
constexpr uint16_t kMaxUtterancePaddingMs = 5000;
constexpr size_t kSamplesPerFrame = (kDefaultSampleRateHz * kFrameDurationMs) / 1000;
constexpr size_t kBytesPerFrame = kSamplesPerFrame * sizeof(int16_t);
constexpr size_t kMaxPreRollFrames = kMaxUtterancePaddingMs / kFrameDurationMs;

constexpr size_t kUploadQueueDepth = 24;
// Batch mic frames before HTTP POST (~50 frames/s cannot sustain one POST per frame).
constexpr size_t kUploadBatchFrames = 8;
constexpr size_t kUploadBatchBytes = kBytesPerFrame * kUploadBatchFrames;
constexpr size_t kPlaybackQueueDepth = 2;
// One queued item must fit in FreeRTOS queue storage (internal RAM). 64 KiB × depth OOMs at boot.
constexpr size_t kMaxPlaybackBytes = 8 * 1024;

struct PcmFrame {
    int16_t samples[kSamplesPerFrame];
};

enum class VadEventType : uint8_t {
    Silence,
    SpeechStart,
    SpeechActive,
    SpeechEnd,
};

struct VadEvent {
    VadEventType type;
};

}  // namespace audio
