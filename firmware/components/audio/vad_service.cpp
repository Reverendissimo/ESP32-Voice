/**
 * @file vad_service.cpp
 * @brief Implementation of VadService.
 */
#include "vad_service.hpp"

#include <stddef.h>

void VadService::configure(const config::VadConfig& vadConfig) {
    m_config = vadConfig;
    m_inSpeech = false;
    m_silenceMs = 0;
}

uint32_t VadService::frameEnergy(const int16_t* samples, size_t sampleCount) const {
    if (samples == nullptr || sampleCount == 0) {
        return 0;
    }

    uint64_t sum = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        const int32_t sample = samples[i];
        sum += static_cast<uint32_t>(sample >= 0 ? sample : -sample);
    }
    return static_cast<uint32_t>(sum / sampleCount);
}

uint32_t VadService::speechEnergyThreshold() const {
    return static_cast<uint32_t>(m_config.speechStartThreshold) * 40U;
}

bool VadService::isSpeechEnergy(uint32_t energy) const {
    return energy >= speechEnergyThreshold();
}

audio::VadEventType VadService::processFrame(const int16_t* samples, size_t sampleCount) {
    const uint32_t energy = frameEnergy(samples, sampleCount);
    const bool speech = isSpeechEnergy(energy);

    if (!m_inSpeech) {
        if (!speech) {
            return audio::VadEventType::Silence;
        }
        m_inSpeech = true;
        m_silenceMs = 0;
        return audio::VadEventType::SpeechStart;
    }

    if (speech) {
        m_silenceMs = 0;
        return audio::VadEventType::SpeechActive;
    }

    m_silenceMs += audio::kFrameDurationMs;
    if (m_silenceMs >= m_config.silenceFinalizeMs) {
        m_inSpeech = false;
        m_silenceMs = 0;
        return audio::VadEventType::SpeechEnd;
    }

    return audio::VadEventType::SpeechActive;
}

bool VadService::isInSpeech() const {
    return m_inSpeech;
}
