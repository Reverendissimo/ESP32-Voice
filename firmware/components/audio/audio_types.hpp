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

constexpr size_t kUploadQueueDepth = 32;
// Batch mic frames before HTTP POST (~50 frames/s cannot sustain one POST per frame).
constexpr size_t kUploadBatchFrames = 4;
constexpr size_t kUploadBatchBytes = kBytesPerFrame * kUploadBatchFrames;
constexpr size_t kPlaybackQueueDepth = 64;
// ~768 ms mono @ 16 kHz per chunk — balances RTT headroom and PSRAM/Wi-Fi pressure.
constexpr size_t kPlaybackChunkBytes = 24576;
constexpr size_t kMaxPlaybackBytes = kPlaybackChunkBytes;
// ~6 s mono @ 16 kHz — enough cushion without starving Wi-Fi internal heap.
constexpr size_t kPlaybackRingBytes = 192 * 1024;
// ~2 s mono @ 16 kHz — absorb slow HTTP round-trips before opening speaker.
constexpr size_t kPlaybackPrefetchBytes = 64 * 1024;
// Codec vol 100 = 0 dB max; gain scales with UI volume slider.
constexpr uint8_t kDefaultPlaybackVolumePercent = 80;
constexpr int kSpeakerVolume = 100;

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
