/**
 * @file audio_playback_service.cpp
 * @brief Implementation of AudioPlaybackService.
 */
#include "audio_playback_service.hpp"

#include <string.h>

#include "box3_audio_board.hpp"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* kTag = "audio_playback";

void AudioPlaybackService::configure(Box3AudioBoard* board, uint16_t defaultSampleRateHz, uint8_t defaultChannels) {
    m_board = board;
    m_defaultSampleRateHz = defaultSampleRateHz;
    m_defaultChannels = defaultChannels;
}

bool AudioPlaybackService::start() {
    if (m_running) {
        return true;
    }
    if (m_board == nullptr || !m_board->isInitialized()) {
        return false;
    }

    m_queue = xQueueCreate(audio::kPlaybackQueueDepth, sizeof(PlaybackJob));
    if (m_queue == nullptr) {
        ESP_LOGE(kTag, "playback queue alloc failed depth=%u", static_cast<unsigned>(audio::kPlaybackQueueDepth));
        return false;
    }

    m_running = true;
    const BaseType_t created = xTaskCreate(
        playbackTask,
        "audio_play",
        4096,
        this,
        4,
        reinterpret_cast<TaskHandle_t*>(&m_taskHandle));
    if (created != pdPASS) {
        m_running = false;
        vQueueDelete(static_cast<QueueHandle_t>(m_queue));
        m_queue = nullptr;
        ESP_LOGE(kTag, "playback task create failed");
        return false;
    }

    ESP_LOGI(kTag, "playback worker started");
    return true;
}

bool AudioPlaybackService::enqueuePcm(
    const int16_t* samples,
    size_t sampleCount,
    uint16_t sampleRateHz,
    uint8_t channels) {
    if (!m_running || samples == nullptr || sampleCount == 0 || m_queue == nullptr) {
        return false;
    }
    if (sampleCount > (audio::kMaxPlaybackBytes / sizeof(int16_t))) {
        ESP_LOGW(kTag, "playback payload too large (%u samples)", static_cast<unsigned>(sampleCount));
        return false;
    }

    const size_t byteLen = sampleCount * sizeof(int16_t);
    auto* copy = static_cast<int16_t*>(heap_caps_malloc(byteLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (copy == nullptr) {
        ESP_LOGW(kTag, "playback PSRAM alloc failed (%u bytes)", static_cast<unsigned>(byteLen));
        return false;
    }
    memcpy(copy, samples, byteLen);

    PlaybackJob job = {
        .sampleRateHz = sampleRateHz,
        .channels = channels,
        .sampleCount = sampleCount,
        .samples = copy,
    };

    if (xQueueSend(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(100)) != pdTRUE) {
        heap_caps_free(copy);
        ESP_LOGW(kTag, "playback queue full");
        return false;
    }
    return true;
}

bool AudioPlaybackService::isRunning() const {
    return m_running;
}

bool AudioPlaybackService::takeJob(PlaybackJob& job) {
    if (m_queue == nullptr) {
        return false;
    }
    return xQueueReceive(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(200)) == pdTRUE;
}

void AudioPlaybackService::freeJob(PlaybackJob& job) {
    if (job.samples != nullptr) {
        heap_caps_free(job.samples);
        job.samples = nullptr;
    }
}

void AudioPlaybackService::playbackTask(void* arg) {
    auto* self = static_cast<AudioPlaybackService*>(arg);
    if (self != nullptr) {
        self->runPlaybackLoop();
    }
    vTaskDelete(nullptr);
}

void AudioPlaybackService::runPlaybackLoop() {
    PlaybackJob job = {};
    while (m_running) {
        if (!takeJob(job)) {
            continue;
        }

        if (job.samples == nullptr || job.sampleCount == 0) {
            freeJob(job);
            continue;
        }

        if (!m_board->openSpeaker(job.sampleRateHz, job.channels)) {
            ESP_LOGW(kTag, "speaker open failed");
            freeJob(job);
            continue;
        }

        esp_codec_dev_handle_t speaker = m_board->speaker();
        esp_codec_dev_set_out_vol(speaker, 70);
        const size_t bytes = job.sampleCount * sizeof(int16_t);
        const int writeRc = esp_codec_dev_write(speaker, job.samples, static_cast<int>(bytes));
        if (writeRc != ESP_CODEC_DEV_OK) {
            ESP_LOGW(kTag, "speaker write failed rc=%d", writeRc);
        }
        esp_codec_dev_set_out_vol(speaker, 0);
        freeJob(job);
    }
}
