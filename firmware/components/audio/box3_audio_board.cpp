/**
 * @file box3_audio_board.cpp
 * @brief Implementation of Box3AudioBoard.
 */
#include "box3_audio_board.hpp"

#include <inttypes.h>

#include "bsp/esp-box-3.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* kTag = "box3_audio";

bool Box3AudioBoard::initialize() {
    if (m_initialized) {
        return true;
    }

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "I2C init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = bsp_audio_init(nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "audio init failed: %s", esp_err_to_name(err));
        return false;
    }

    // BSP display_audio_photo: speaker init first, then mic; gains set once before open.
    m_speaker = bsp_audio_codec_speaker_init();
    if (m_speaker == nullptr) {
        ESP_LOGE(kTag, "speaker codec init failed");
        return false;
    }
    esp_codec_dev_set_out_vol(m_speaker, 100);

    m_microphone = bsp_audio_codec_microphone_init();
    if (m_microphone == nullptr) {
        ESP_LOGE(kTag, "microphone codec init failed");
        return false;
    }
    esp_codec_dev_set_in_gain(m_microphone, 50.0);

    m_initialized = true;
    ESP_LOGI(kTag, "BOX-3 audio ready");
    return true;
}

bool Box3AudioBoard::openDevice(
    esp_codec_dev_handle_t device,
    uint16_t sampleRateHz,
    uint8_t channels,
    bool isMic) {
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = channels,
        .channel_mask = 0,
        .sample_rate = sampleRateHz,
        .mclk_multiple = I2S_MCLK_MULTIPLE_384,
    };

    if (!isMic) {
        esp_codec_dev_set_out_vol(device, 100);
    }

    if (esp_codec_dev_open(device, &format) != ESP_CODEC_DEV_OK) {
        return false;
    }
    return true;
}

bool Box3AudioBoard::openMicrophone(uint16_t sampleRateHz, uint8_t channels) {
    (void)channels;
    if (!m_initialized || m_microphone == nullptr) {
        return false;
    }
    if (m_micOpen) {
        return true;
    }

    constexpr uint8_t kHwChannels = 1;
    if (!openDevice(m_microphone, sampleRateHz, kHwChannels, true)) {
        ESP_LOGE(kTag, "mic open failed");
        return false;
    }

    m_sampleRateHz = sampleRateHz;
    m_channels = kHwChannels;
    m_micOpen = true;
    ESP_LOGI(
        kTag,
        "mic open %u Hz %uch",
        static_cast<unsigned>(sampleRateHz),
        static_cast<unsigned>(kHwChannels));
    return true;
}

bool Box3AudioBoard::ensureSpeakerInitialized() {
    if (m_speaker != nullptr) {
        return true;
    }
    m_speaker = bsp_audio_codec_speaker_init();
    if (m_speaker == nullptr) {
        ESP_LOGE(kTag, "speaker codec init failed");
        return false;
    }
    return true;
}

bool Box3AudioBoard::openSpeaker(uint16_t sampleRateHz, uint8_t channels) {
    if (!m_initialized || !ensureSpeakerInitialized()) {
        return false;
    }
    if (m_speakerOpen) {
        return true;
    }

    if (!openDevice(m_speaker, sampleRateHz, channels, false)) {
        ESP_LOGE(kTag, "speaker open failed");
        return false;
    }

    m_speakerOpen = true;
    ESP_LOGI(kTag, "speaker open %u Hz", static_cast<unsigned>(sampleRateHz));
    return true;
}

void Box3AudioBoard::closeMicrophone() {
    if (!m_micOpen || m_microphone == nullptr) {
        return;
    }
    esp_codec_dev_close(m_microphone);
    m_micOpen = false;
}

void Box3AudioBoard::closeSpeaker() {
    if (!m_speakerOpen || m_speaker == nullptr) {
        return;
    }
    esp_codec_dev_close(m_speaker);
    m_speakerOpen = false;
}

esp_codec_dev_handle_t Box3AudioBoard::microphone() {
    return m_microphone;
}

esp_codec_dev_handle_t Box3AudioBoard::speaker() {
    return m_speaker;
}

bool Box3AudioBoard::isInitialized() const {
    return m_initialized;
}

bool Box3AudioBoard::isMicrophoneOpen() const {
    return m_micOpen;
}

bool Box3AudioBoard::isSpeakerOpen() const {
    return m_speakerOpen;
}

uint8_t Box3AudioBoard::micChannels() const {
    return m_channels;
}

bool Box3AudioBoard::probeMicrophonePcm(uint32_t* outMaxAbs) {
    if (!m_micOpen || m_microphone == nullptr) {
        return false;
    }

    constexpr size_t kProbeBytes = 1024;
    auto* buffer = static_cast<int16_t*>(
        heap_caps_malloc(kProbeBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        ESP_LOGE(kTag, "mic probe buffer alloc failed");
        return false;
    }

    uint32_t maxAbs = 0;
    uint32_t okReads = 0;
    for (int i = 0; i < 5; ++i) {
        const int readRc = esp_codec_dev_read(m_microphone, buffer, static_cast<int>(kProbeBytes));
        if (readRc != ESP_CODEC_DEV_OK) {
            continue;
        }
        ++okReads;
        const size_t sampleCount = kProbeBytes / sizeof(int16_t);
        for (size_t s = 0; s < sampleCount; ++s) {
            const int32_t sample = buffer[s];
            const uint32_t absSample = static_cast<uint32_t>(sample >= 0 ? sample : -sample);
            if (absSample > maxAbs) {
                maxAbs = absSample;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    heap_caps_free(buffer);
    if (outMaxAbs != nullptr) {
        *outMaxAbs = maxAbs;
    }

    ESP_LOGI(kTag, "mic boot probe ok_reads=%" PRIu32 " max_abs=%" PRIu32, okReads, maxAbs);
    return okReads > 0;
}
