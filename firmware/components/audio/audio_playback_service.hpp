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

class Box3AudioBoard;

/**
 * @brief Plays async audio commanded by server.
 */
class AudioPlaybackService {
public:
    void configure(Box3AudioBoard* board, uint16_t defaultSampleRateHz, uint8_t defaultChannels);
    bool start();
    bool enqueuePcm(const int16_t* samples, size_t sampleCount, uint16_t sampleRateHz, uint8_t channels);
    bool isRunning() const;

private:
    struct PlaybackJob {
        uint16_t sampleRateHz;
        uint8_t channels;
        size_t sampleCount;
        int16_t* samples;
    };

    static void playbackTask(void* arg);
    void runPlaybackLoop();
    bool takeJob(PlaybackJob& job);
    void freeJob(PlaybackJob& job);

    Box3AudioBoard* m_board = nullptr;
    uint16_t m_defaultSampleRateHz = audio::kDefaultSampleRateHz;
    uint8_t m_defaultChannels = audio::kDefaultChannels;
    bool m_running = false;
    void* m_taskHandle = nullptr;
    void* m_queue = nullptr;
};
