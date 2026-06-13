/**
 * @file audio_playback_service.hpp
 * @brief Plays async audio commanded by server.
 *
 * Responsibilities:
 * - queue and play remote audio payloads
 *
 * Non-responsibilities:
 * - speech capture
 * - display rendering
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_types.hpp"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Box3AudioBoard;
class AudioCaptureService;

/**
 * @brief Plays async audio commanded by server.
 */
class AudioPlaybackService {
public:
    void configure(
        Box3AudioBoard* board,
        uint16_t defaultSampleRateHz,
        uint8_t defaultChannels,
        AudioCaptureService* capture = nullptr);
    bool start();
    bool enqueuePcm(const int16_t* samples, size_t sampleCount, uint16_t sampleRateHz, uint8_t channels);
    bool enqueueDecodedPcm(
        int16_t* samples,
        size_t sampleCount,
        uint16_t sampleRateHz,
        uint8_t channels,
        bool streamEnd = false);
    void endStream();
    bool isRunning() const;
    bool isBusy() const;
    size_t ringUsedBytes() const;
    size_t ringFreeBytes() const;
    size_t ringCapacityBytes() const;
    void setVolumePercent(uint8_t percent);
    uint8_t volumePercent() const;

private:
    static void playbackTask(void* arg);
    void runPlaybackLoop();
    bool writeRing(const uint8_t* data, size_t byteLen, TickType_t timeoutTicks);
    size_t readRing(uint8_t* out, size_t maxBytes);
    void drainSpeaker(esp_codec_dev_handle_t speaker);
    void onPlaybackIdle();

    Box3AudioBoard* m_board = nullptr;
    AudioCaptureService* m_capture = nullptr;
    uint16_t m_defaultSampleRateHz = audio::kDefaultSampleRateHz;
    uint8_t m_defaultChannels = audio::kDefaultChannels;
    bool m_running = false;
    bool m_playing = false;
    bool m_expectMoreChunks = false;
    void* m_taskHandle = nullptr;
    uint8_t* m_ring = nullptr;
    size_t m_ringCapacity = 0;
    size_t m_ringWritePos = 0;
    size_t m_ringReadPos = 0;
    size_t m_ringUsed = 0;
    void* m_ringMutex = nullptr;
    uint16_t m_streamSampleRateHz = audio::kDefaultSampleRateHz;
    uint8_t m_streamChannels = audio::kDefaultChannels;
    bool m_streamFormatSet = false;
    uint8_t m_volumePercent = audio::kDefaultPlaybackVolumePercent;
    float m_pcmGain = 2.5f;
};
