/**
 * @file vad_service.hpp
 * @brief Detects speech activity in captured audio.
 *
 * Responsibilities:
 * - speech start/stop events
 *
 * Non-responsibilities:
 * - upload transport
 * - playback
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "audio_types.hpp"
#include "config_models.hpp"

/**
 * @brief Detects speech activity in captured audio frames.
 */
class VadService {
public:
    /**
     * @brief Applies VAD configuration thresholds.
     */
    void configure(const config::VadConfig& vadConfig);

    /**
     * @brief Processes one PCM frame and returns a VAD event.
     */
    audio::VadEventType processFrame(const int16_t* samples, size_t sampleCount);

    uint32_t frameEnergy(const int16_t* samples, size_t sampleCount) const;
    uint32_t speechEnergyThreshold() const;

    bool isInSpeech() const;

private:
    bool isSpeechEnergy(uint32_t energy) const;

    config::VadConfig m_config = {};
    bool m_inSpeech = false;
    uint32_t m_silenceMs = 0;
};
