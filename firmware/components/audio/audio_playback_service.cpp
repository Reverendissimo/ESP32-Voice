/**
 * @file audio_playback_service.cpp
 * @brief Implementation of AudioPlaybackService.
 */
#include "audio_playback_service.hpp"

#include <string.h>

#include "audio_capture_service.hpp"
#include "box3_audio_board.hpp"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* kTag = "audio_playback";

namespace {

constexpr size_t kCodecWriteBytes = 4096;
constexpr int kEnqueueTimeoutMs = 150;
constexpr int kPlaybackTaskPriority = 7;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr uint32_t kTailPollsBeforeDrain = 50;
constexpr uint32_t kStreamTailTimeoutPolls = 500;

void amplifyPcm(int16_t* samples, size_t count, float gain) {
    for (size_t i = 0; i < count; ++i) {
        const int32_t boosted = static_cast<int32_t>(static_cast<float>(samples[i]) * gain);
        if (boosted > 32767) {
            samples[i] = 32767;
        } else if (boosted < -32768) {
            samples[i] = -32768;
        } else {
            samples[i] = static_cast<int16_t>(boosted);
        }
    }
}

float gainFromVolumePercent(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    return 0.5f + (static_cast<float>(percent) / 100.0f) * 3.0f;
}

void setCapturePaused(AudioCaptureService* capture, bool paused) {
    if (capture != nullptr) {
        capture->setPlaybackPaused(paused);
    }
}

}  // namespace

void AudioPlaybackService::setVolumePercent(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    m_volumePercent = percent;
    m_pcmGain = gainFromVolumePercent(percent);
}

uint8_t AudioPlaybackService::volumePercent() const {
    return m_volumePercent;
}

void AudioPlaybackService::setActivityCallbacks(const AudioActivityCallbacks& callbacks) {
    m_activity = callbacks;
}

void AudioPlaybackService::notifyActivity(AudioActivity activity) {
    if (m_activity.onActivity != nullptr) {
        m_activity.onActivity(m_activity.context, activity);
    }
}

void AudioPlaybackService::configure(
    Box3AudioBoard* board,
    uint16_t defaultSampleRateHz,
    uint8_t defaultChannels,
    AudioCaptureService* capture) {
    m_board = board;
    m_capture = capture;
    m_defaultSampleRateHz = defaultSampleRateHz;
    m_defaultChannels = defaultChannels;
    setVolumePercent(audio::kDefaultPlaybackVolumePercent);
}

bool AudioPlaybackService::start() {
    if (m_running) {
        return true;
    }
    if (m_board == nullptr || !m_board->isInitialized()) {
        return false;
    }

    m_ringCapacity = audio::kPlaybackRingBytes;
    if (m_ring == nullptr) {
        m_ring = static_cast<uint8_t*>(heap_caps_malloc(m_ringCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (m_ioBuffer == nullptr) {
        m_ioBuffer = static_cast<uint8_t*>(heap_caps_malloc(kCodecWriteBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (m_ring == nullptr || m_ioBuffer == nullptr) {
        ESP_LOGE(kTag, "playback PSRAM alloc failed (ring=%u io=%u)", static_cast<unsigned>(m_ringCapacity), static_cast<unsigned>(kCodecWriteBytes));
        return false;
    }
    m_ringWritePos = 0;
    m_ringReadPos = 0;
    m_ringUsed = 0;
    m_streamFormatSet = false;

    if (m_ringMutex == nullptr) {
        m_ringMutex = xSemaphoreCreateMutex();
    }
    if (m_ringMutex == nullptr) {
        ESP_LOGE(kTag, "playback ring mutex alloc failed");
        return false;
    }

    m_running = true;
    const BaseType_t created = xTaskCreatePinnedToCore(
        playbackTask,
        "audio_play",
        8192,
        this,
        kPlaybackTaskPriority,
        reinterpret_cast<TaskHandle_t*>(&m_taskHandle),
        kAudioTaskCore);
    if (created != pdPASS) {
        m_running = false;
        ESP_LOGE(kTag, "playback task create failed");
        return false;
    }

    ESP_LOGI(
        kTag,
        "playback worker started ring_bytes=%u core=%d",
        static_cast<unsigned>(m_ringCapacity),
        static_cast<int>(kAudioTaskCore));
    return true;
}

size_t AudioPlaybackService::ringUsedBytes() const {
    if (m_ringMutex == nullptr) {
        return 0;
    }
    if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(m_ringMutex), pdMS_TO_TICKS(20)) != pdTRUE) {
        return m_ringUsed;
    }
    const size_t used = m_ringUsed;
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(m_ringMutex));
    return used;
}

bool AudioPlaybackService::writeRing(const uint8_t* data, size_t byteLen, TickType_t timeoutTicks) {
    if (m_ring == nullptr || m_ringMutex == nullptr || data == nullptr || byteLen == 0) {
        return false;
    }

    const TickType_t start = xTaskGetTickCount();
    size_t written = 0;
    while (written < byteLen) {
        if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(m_ringMutex), pdMS_TO_TICKS(20)) != pdTRUE) {
            if (timeoutTicks == 0 || (xTaskGetTickCount() - start) >= timeoutTicks) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        const size_t freeBytes = m_ringCapacity - m_ringUsed;
        size_t chunk = byteLen - written;
        if (chunk > freeBytes) {
            chunk = freeBytes;
        }
        if (chunk > 0) {
            const size_t untilEnd = m_ringCapacity - m_ringWritePos;
            const size_t first = (chunk > untilEnd) ? untilEnd : chunk;
            memcpy(m_ring + m_ringWritePos, data + written, first);
            if (chunk > first) {
                memcpy(m_ring, data + written + first, chunk - first);
            }
            m_ringWritePos = (m_ringWritePos + chunk) % m_ringCapacity;
            m_ringUsed += chunk;
            written += chunk;
        }
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(m_ringMutex));

        if (written >= byteLen) {
            return true;
        }

        if (timeoutTicks == 0 || (xTaskGetTickCount() - start) >= timeoutTicks) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

size_t AudioPlaybackService::readRing(uint8_t* out, size_t maxBytes) {
    if (m_ring == nullptr || m_ringMutex == nullptr || out == nullptr || maxBytes == 0) {
        return 0;
    }
    if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(m_ringMutex), pdMS_TO_TICKS(20)) != pdTRUE) {
        return 0;
    }

    size_t chunk = maxBytes;
    if (chunk > m_ringUsed) {
        chunk = m_ringUsed;
    }
    if (chunk > 0) {
        const size_t untilEnd = m_ringCapacity - m_ringReadPos;
        const size_t first = (chunk > untilEnd) ? untilEnd : chunk;
        memcpy(out, m_ring + m_ringReadPos, first);
        if (chunk > first) {
            memcpy(out + first, m_ring, chunk - first);
        }
        m_ringReadPos = (m_ringReadPos + chunk) % m_ringCapacity;
        m_ringUsed -= chunk;
    }
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(m_ringMutex));
    return chunk;
}

void AudioPlaybackService::wakePlaybackTask() {
    if (m_taskHandle != nullptr) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(m_taskHandle));
    }
}

void AudioPlaybackService::preparePlayback(uint16_t sampleRateHz, uint8_t channels) {
    setCapturePaused(m_capture, true);
    if (m_board != nullptr) {
        if (m_board->isMicrophoneOpen()) {
            m_board->closeMicrophone();
        }
        m_micSuspendedForPlayback = true;
    }
    notifyActivity(AudioActivity::Speaking);
    if (!m_streamFormatSet) {
        m_streamSampleRateHz = sampleRateHz;
        m_streamChannels = channels;
        m_streamFormatSet = true;
    }
    wakePlaybackTask();
}

bool AudioPlaybackService::enqueueDecodedPcm(
    int16_t* samples,
    size_t sampleCount,
    uint16_t sampleRateHz,
    uint8_t channels,
    bool streamEnd) {
    if (!m_running || samples == nullptr || sampleCount == 0 || m_ring == nullptr) {
        return false;
    }
    if (sampleCount > (audio::kMaxPlaybackBytes / sizeof(int16_t))) {
        ESP_LOGW(kTag, "playback payload too large (%u samples)", static_cast<unsigned>(sampleCount));
        return false;
    }

    if (!m_streamFormatSet) {
        preparePlayback(sampleRateHz, channels);
    } else {
        setCapturePaused(m_capture, true);
        wakePlaybackTask();
    }
    m_expectMoreChunks = !streamEnd;

    amplifyPcm(samples, sampleCount, m_pcmGain);
    const size_t byteLen = sampleCount * sizeof(int16_t);
    const bool ok = writeRing(
        reinterpret_cast<const uint8_t*>(samples),
        byteLen,
        pdMS_TO_TICKS(kEnqueueTimeoutMs));
    if (!ok) {
        ESP_LOGW(kTag, "playback ring full after %d ms", kEnqueueTimeoutMs);
    }
    return ok;
}

void AudioPlaybackService::endStream() {
    m_expectMoreChunks = false;
    wakePlaybackTask();
}

bool AudioPlaybackService::enqueuePcm(
    const int16_t* samples,
    size_t sampleCount,
    uint16_t sampleRateHz,
    uint8_t channels) {
    if (!m_running || samples == nullptr || sampleCount == 0 || m_ring == nullptr) {
        return false;
    }
    if (sampleCount > (audio::kMaxPlaybackBytes / sizeof(int16_t))) {
        ESP_LOGW(kTag, "playback payload too large (%u samples)", static_cast<unsigned>(sampleCount));
        return false;
    }

    if (!m_streamFormatSet) {
        preparePlayback(sampleRateHz, channels);
    } else {
        setCapturePaused(m_capture, true);
        wakePlaybackTask();
    }

    const size_t byteLen = sampleCount * sizeof(int16_t);
    auto* copy = static_cast<int16_t*>(heap_caps_malloc(byteLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (copy == nullptr) {
        ESP_LOGW(kTag, "playback PSRAM alloc failed (%u bytes)", static_cast<unsigned>(byteLen));
        return false;
    }
    memcpy(copy, samples, byteLen);
    amplifyPcm(copy, sampleCount, m_pcmGain);

    const bool ok = writeRing(
        reinterpret_cast<const uint8_t*>(copy),
        byteLen,
        pdMS_TO_TICKS(kEnqueueTimeoutMs));
    heap_caps_free(copy);

    if (!ok) {
        ESP_LOGW(kTag, "playback ring full after %d ms", kEnqueueTimeoutMs);
        return false;
    }
    return true;
}

bool AudioPlaybackService::isRunning() const {
    return m_running;
}

bool AudioPlaybackService::isBusy() const {
    return m_playing || ringUsedBytes() > 0 || m_expectMoreChunks;
}

size_t AudioPlaybackService::ringFreeBytes() const {
    const size_t used = ringUsedBytes();
    if (used >= m_ringCapacity) {
        return 0;
    }
    return m_ringCapacity - used;
}

size_t AudioPlaybackService::ringCapacityBytes() const {
    return m_ringCapacity;
}

void AudioPlaybackService::drainSpeaker(esp_codec_dev_handle_t speaker) {
    if (speaker == nullptr) {
        return;
    }
    int16_t silence[512] = {};
    for (int i = 0; i < 8; ++i) {
        (void)esp_codec_dev_write(speaker, silence, static_cast<int>(sizeof(silence)));
    }
}

void AudioPlaybackService::reopenMicrophone() {
    if (m_board == nullptr) {
        return;
    }
    if (m_board->isMicrophoneOpen()) {
        m_micSuspendedForPlayback = false;
        return;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        if (m_board->openMicrophone(m_defaultSampleRateHz, m_defaultChannels)) {
            m_micSuspendedForPlayback = false;
            ESP_LOGI(kTag, "mic reopened after playback");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    ESP_LOGW(kTag, "mic reopen failed after playback");
    m_micSuspendedForPlayback = false;
}

void AudioPlaybackService::onPlaybackIdle() {
    if (m_board == nullptr) {
        setCapturePaused(m_capture, false);
        m_expectMoreChunks = false;
        m_micSuspendedForPlayback = false;
        m_streamWaitPolls = 0;
        notifyActivity(AudioActivity::Listening);
        return;
    }
    if (m_board->isSpeakerOpen()) {
        drainSpeaker(m_board->speaker());
        vTaskDelay(pdMS_TO_TICKS(40));
        m_board->closeSpeaker();
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    reopenMicrophone();
    setCapturePaused(m_capture, false);
    m_streamFormatSet = false;
    m_expectMoreChunks = false;
    m_streamWaitPolls = 0;
    notifyActivity(AudioActivity::Listening);
    ESP_LOGI(kTag, "playback idle — mic resumed");
}

void AudioPlaybackService::playbackTask(void* arg) {
    auto* self = static_cast<AudioPlaybackService*>(arg);
    if (self != nullptr) {
        self->runPlaybackLoop();
    }
    vTaskDelete(nullptr);
}

void AudioPlaybackService::runPlaybackLoop() {
    uint32_t idlePolls = 0;

    while (m_running) {
        const size_t pending = ringUsedBytes();
        if (pending == 0) {
            if (m_playing) {
                if (m_expectMoreChunks) {
                    ++m_streamWaitPolls;
                    if (m_streamWaitPolls >= kStreamTailTimeoutPolls) {
                        ESP_LOGW(kTag, "stream tail timeout — forcing playback idle");
                        m_expectMoreChunks = false;
                        m_streamWaitPolls = 0;
                    } else {
                        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
                        continue;
                    }
                }
                ++idlePolls;
                if (idlePolls >= kTailPollsBeforeDrain) {
                    m_playing = false;
                    onPlaybackIdle();
                    idlePolls = 0;
                } else {
                    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
                }
            } else {
                m_streamWaitPolls = 0;
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            }
            continue;
        }
        idlePolls = 0;
        m_streamWaitPolls = 0;

        if (!m_playing) {
            const size_t prefetch =
                m_expectMoreChunks ? audio::kPlaybackPrefetchBytes : 0;
            if (m_expectMoreChunks && pending < prefetch) {
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
                continue;
            }
            m_playing = true;
            if (!m_board->openSpeaker(m_streamSampleRateHz, m_streamChannels)) {
                ESP_LOGW(kTag, "speaker open failed");
                m_playing = false;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            esp_codec_dev_set_out_vol(m_board->speaker(), audio::kSpeakerVolume);
            ESP_LOGI(
                kTag,
                "stream play %u Hz pending=%u bytes",
                static_cast<unsigned>(m_streamSampleRateHz),
                static_cast<unsigned>(pending));
        }

        const size_t toRead = (pending > kCodecWriteBytes) ? kCodecWriteBytes : pending;
        const size_t got = readRing(m_ioBuffer, toRead);
        if (got == 0) {
            continue;
        }

        const int writeRc = esp_codec_dev_write(
            m_board->speaker(),
            m_ioBuffer,
            static_cast<int>(got));
        if (writeRc != ESP_CODEC_DEV_OK) {
            ESP_LOGW(kTag, "speaker write failed rc=%d", writeRc);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}
