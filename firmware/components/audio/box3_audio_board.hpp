/**
 * @file box3_audio_board.hpp
 * @brief Thin wrapper around ESP32-S3-BOX-3 BSP audio codec devices.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev.h"

/**
 * @brief BOX-3 microphone and speaker codec access.
 */
class Box3AudioBoard {
public:
    bool initialize();
    bool openMicrophone(uint16_t sampleRateHz, uint8_t channels);
    bool openSpeaker(uint16_t sampleRateHz, uint8_t channels);
    void closeMicrophone();
    void closeSpeaker();

    esp_codec_dev_handle_t microphone();
    esp_codec_dev_handle_t speaker();

    bool isInitialized() const;
    bool isMicrophoneOpen() const;
    bool isSpeakerOpen() const;
    uint8_t micChannels() const;

    /**
     * @brief Synchronous mic read smoke test (BSP display_audio_photo pattern).
     * @param outMaxAbs Peak absolute sample value observed.
     * @return true when at least one read succeeded.
     */
    bool probeMicrophonePcm(uint32_t* outMaxAbs);

private:
    bool openDevice(esp_codec_dev_handle_t device, uint16_t sampleRateHz, uint8_t channels, bool isMic);
    bool ensureSpeakerInitialized();

    bool m_initialized = false;
    bool m_micOpen = false;
    bool m_speakerOpen = false;
    uint16_t m_sampleRateHz = 16000;
    uint8_t m_channels = 1;
    esp_codec_dev_handle_t m_microphone = nullptr;
    esp_codec_dev_handle_t m_speaker = nullptr;
};
