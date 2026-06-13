/**
 * @file audio_capture_service.cpp
 * @brief Implementation of AudioCaptureService.
 */
#include "audio_capture_service.hpp"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "audio_types.hpp"
#include "audio_upload_service.hpp"
#include "box3_audio_board.hpp"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utterance_state_machine.hpp"
#include "vad_service.hpp"

static const char* kTag = "audio_capture";

void AudioCaptureService::configure(
    Box3AudioBoard* board,
    VadService* vad,
    UtteranceStateMachine* utteranceFsm,
    AudioUploadService* upload,
    const config::AudioConfig& audio,
    const config::VadConfig& vadConfig) {
    m_board = board;
    m_vad = vad;
    m_utteranceFsm = utteranceFsm;
    m_upload = upload;
    m_audio = audio;
    m_vadConfig = vadConfig;
    if (m_vad != nullptr) {
        m_vad->configure(m_vadConfig);
    }
    updatePreRollCapacity();
}

void AudioCaptureService::updatePreRollCapacity() {
    m_preRollFrameCount =
        static_cast<size_t>(m_vadConfig.preRollPaddingMs / audio::kFrameDurationMs);
    if (m_preRollFrameCount > audio::kMaxPreRollFrames) {
        m_preRollFrameCount = audio::kMaxPreRollFrames;
    }
}

bool AudioCaptureService::start() {
    if (m_running) {
        return true;
    }
    if (m_board == nullptr || m_vad == nullptr || m_utteranceFsm == nullptr || m_upload == nullptr ||
        !m_board->isInitialized() || !m_board->isMicrophoneOpen()) {
        return false;
    }

    const uint8_t hwChannels = m_board->micChannels();
    const size_t channels = hwChannels > 0 ? static_cast<size_t>(hwChannels) : 1U;
    const size_t requiredReadBytes = audio::kBytesPerFrame * channels;
    if (m_readBuffer != nullptr && m_readBufferSize != requiredReadBytes) {
        heap_caps_free(m_readBuffer);
        m_readBuffer = nullptr;
    }
    m_readBufferSize = requiredReadBytes;
    if (m_readBuffer == nullptr) {
        m_readBuffer = static_cast<uint8_t*>(
            heap_caps_malloc(m_readBufferSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (m_readBuffer == nullptr) {
        ESP_LOGE(kTag, "DMA read buffer alloc failed (%u bytes)", static_cast<unsigned>(m_readBufferSize));
        return false;
    }
    if (m_preRollRing == nullptr) {
        m_preRollRing = static_cast<audio::PcmFrame*>(heap_caps_malloc(
            audio::kMaxPreRollFrames * sizeof(audio::PcmFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (m_preRollRing == nullptr) {
            ESP_LOGE(kTag, "pre-roll ring PSRAM alloc failed");
            return false;
        }
    }
    m_preRollWriteIdx = 0;
    m_preRollFull = false;

    m_vad->configure(m_vadConfig);
    m_lastEnergy = 0;
    m_peakEnergy = 0;
    m_readFailCount = 0;
    m_framesOkCount = 0;
    m_maxSampleAbs = 0;
    m_running = true;

    const BaseType_t created = xTaskCreate(
        captureTask,
        "audio_cap",
        6144,
        this,
        5,
        reinterpret_cast<TaskHandle_t*>(&m_taskHandle));
    if (created != pdPASS) {
        m_running = false;
        ESP_LOGE(
            kTag,
            "task create failed heap_int=%lu",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return false;
    }

    ESP_LOGI(
        kTag,
        "capture started %u Hz frame_bytes=%u hw_channels=%u pre_roll_ms=%u post_roll_ms=%u",
        static_cast<unsigned>(m_audio.sampleRateHz),
        static_cast<unsigned>(audio::kBytesPerFrame),
        static_cast<unsigned>(hwChannels),
        static_cast<unsigned>(m_vadConfig.preRollPaddingMs),
        static_cast<unsigned>(m_vadConfig.postRollPaddingMs));
    return true;
}

void AudioCaptureService::stop() {
    (void)stopGracefully(1500);
}

bool AudioCaptureService::stopGracefully(uint32_t timeoutMs) {
    if (!m_running) {
        return true;
    }
    m_running = false;

    const int maxSteps = static_cast<int>(timeoutMs / 10U);
    for (int i = 0; i < maxSteps; ++i) {
        if (m_taskHandle == nullptr) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (m_taskHandle != nullptr) {
        ESP_LOGW(
            kTag,
            "capture task still blocked after %" PRIu32 " ms — reboot to recover (not force-deleting)",
            timeoutMs);
        return false;
    }
    return true;
}

bool AudioCaptureService::isRunning() const {
    return m_running && m_taskHandle != nullptr;
}

void AudioCaptureService::setPlaybackPaused(bool paused) {
    m_playbackPaused = paused;
}

void AudioCaptureService::setUserMuted(bool muted) {
    m_userMuted = muted;
    if (muted && m_utteranceFsm != nullptr && m_utteranceFsm->isStreaming()) {
        m_utteranceFsm->cancelActive();
    }
}

bool AudioCaptureService::isUserMuted() const {
    return m_userMuted;
}

uint32_t AudioCaptureService::lastFrameEnergy() const {
    return m_lastEnergy;
}

uint32_t AudioCaptureService::peakFrameEnergy() const {
    return m_peakEnergy;
}

uint32_t AudioCaptureService::readFailCount() const {
    return m_readFailCount;
}

uint32_t AudioCaptureService::framesOkCount() const {
    return m_framesOkCount;
}

uint32_t AudioCaptureService::maxSampleAbs() const {
    return m_maxSampleAbs;
}

void AudioCaptureService::pushPreRoll(const audio::PcmFrame& frame) {
    if (m_preRollRing == nullptr || m_preRollFrameCount == 0) {
        return;
    }
    m_preRollRing[m_preRollWriteIdx] = frame;
    m_preRollWriteIdx = (m_preRollWriteIdx + 1) % m_preRollFrameCount;
    if (m_preRollWriteIdx == 0) {
        m_preRollFull = true;
    }
}

void AudioCaptureService::flushPreRoll() {
    if (m_preRollRing == nullptr || m_preRollFrameCount == 0 || m_utteranceFsm == nullptr ||
        m_upload == nullptr) {
        return;
    }

    const size_t frameCount = m_preRollFull ? m_preRollFrameCount : m_preRollWriteIdx;
    const size_t startIdx = m_preRollFull ? m_preRollWriteIdx : 0;
    for (size_t i = 0; i < frameCount; ++i) {
        const size_t idx = (startIdx + i) % m_preRollFrameCount;
        m_upload->uploadChunk(
            m_utteranceFsm->utteranceId(),
            m_utteranceFsm->sessionId(),
            reinterpret_cast<const uint8_t*>(m_preRollRing[idx].samples),
            audio::kBytesPerFrame);
    }
    ESP_LOGI(kTag, "pre-roll flushed frames=%u", static_cast<unsigned>(frameCount));
}

void AudioCaptureService::captureTask(void* arg) {
    auto* self = static_cast<AudioCaptureService*>(arg);
    if (self != nullptr) {
        self->runCaptureLoop();
        self->m_taskHandle = nullptr;
    }
    vTaskDelete(nullptr);
}

void AudioCaptureService::runCaptureLoop() {
    esp_codec_dev_handle_t mic = m_board->microphone();
    if (mic == nullptr || m_readBuffer == nullptr) {
        ESP_LOGE(kTag, "microphone handle or read buffer missing");
        m_running = false;
        return;
    }

    const uint8_t hwChannels = m_board->micChannels();
    const size_t monoBytes = audio::kBytesPerFrame;
    const size_t hwFrameBytes = monoBytes * (hwChannels > 0 ? static_cast<size_t>(hwChannels) : 1U);
    if (m_readBufferSize != hwFrameBytes) {
        ESP_LOGE(kTag, "read buffer size mismatch");
        m_running = false;
        return;
    }
    audio::PcmFrame frame = {};

    while (m_running) {
        if (m_playbackPaused || m_userMuted) {
            vTaskDelay(pdMS_TO_TICKS(audio::kFrameDurationMs));
            continue;
        }

        const int readRc = esp_codec_dev_read(mic, m_readBuffer, static_cast<int>(hwFrameBytes));
        if (readRc != ESP_CODEC_DEV_OK) {
            ++m_readFailCount;
            if (m_readFailCount == 1 || (m_readFailCount % 500U) == 0U) {
                ESP_LOGW(
                    kTag,
                    "mic read failed rc=%d count=%" PRIu32,
                    readRc,
                    static_cast<uint32_t>(m_readFailCount));
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        m_framesOkCount = m_framesOkCount + 1U;
        const int16_t* raw = reinterpret_cast<const int16_t*>(m_readBuffer);
        const size_t monoSampleCount = monoBytes / sizeof(int16_t);

        if (hwChannels > 1) {
            for (size_t i = 0; i < monoSampleCount; ++i) {
                const int32_t left = raw[i * 2];
                const int32_t right = raw[i * 2 + 1];
                const int32_t mixed = (left + right) / 2;
                frame.samples[i] = static_cast<int16_t>(mixed);
            }
        } else {
            memcpy(frame.samples, raw, monoBytes);
        }

        uint32_t frameMaxAbs = 0;
        for (size_t i = 0; i < monoSampleCount; ++i) {
            const int32_t sample = frame.samples[i];
            const uint32_t absSample = static_cast<uint32_t>(sample >= 0 ? sample : -sample);
            if (absSample > frameMaxAbs) {
                frameMaxAbs = absSample;
            }
        }
        if (frameMaxAbs > m_maxSampleAbs) {
            m_maxSampleAbs = frameMaxAbs;
        }

        const uint32_t energy = m_vad->frameEnergy(frame.samples, monoSampleCount);
        m_lastEnergy = energy;
        if (energy > m_peakEnergy) {
            m_peakEnergy = energy;
        }

        if ((m_framesOkCount % 250U) == 0U) {
            ESP_LOGI(
                kTag,
                "mic frame=%" PRIu32 " energy=%" PRIu32 " max_abs=%" PRIu32,
                static_cast<uint32_t>(m_framesOkCount),
                energy,
                frameMaxAbs);
        }

        const audio::VadEventType event = m_vad->processFrame(frame.samples, monoSampleCount);

        if (event == audio::VadEventType::SpeechStart && m_utteranceFsm->state() == UtteranceState::Idle) {
            m_utteranceFsm->handleVadEvent(event, audio::kFrameDurationMs);
            flushPreRoll();
        } else {
            m_utteranceFsm->handleVadEvent(event, audio::kFrameDurationMs);
        }
        m_utteranceFsm->onFrameTick(audio::kFrameDurationMs);

        if (m_utteranceFsm->shouldUpload()) {
            m_upload->uploadChunk(
                m_utteranceFsm->utteranceId(),
                m_utteranceFsm->sessionId(),
                reinterpret_cast<const uint8_t*>(frame.samples),
                monoBytes);
        }

        pushPreRoll(frame);
    }
}
