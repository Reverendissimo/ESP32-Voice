/**
 * @file audio_upload_service.cpp
 * @brief Implementation of AudioUploadService.
 */
#include "audio_upload_service.hpp"

#include <string.h>

#include "audio_protocol.hpp"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "utterance_state_machine.hpp"

static const char* kTag = "audio_upload";

namespace {

bool isUrlConfigured(const char* url) {
    return url != nullptr && url[0] != '\0';
}

esp_http_client_handle_t makeHttpClient(const char* url, int timeoutMs) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.transport_type = HTTP_TRANSPORT_OVER_TCP;
    config.timeout_ms = timeoutMs;
    config.buffer_size = 2048;
    config.buffer_size_tx = 2048;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    return esp_http_client_init(&config);
}

}  // namespace

void AudioUploadService::configure(
    const config::CallbacksConfig& callbacks,
    const config::AudioConfig& audio,
    const char* deviceUid,
    const char* deviceName,
    const char* authToken) {
    m_callbacks = callbacks;
    m_audio = audio;
    strncpy(m_deviceUid, deviceUid != nullptr ? deviceUid : "", sizeof(m_deviceUid) - 1);
    strncpy(m_deviceName, deviceName != nullptr ? deviceName : "", sizeof(m_deviceName) - 1);
    strncpy(m_authToken, authToken != nullptr ? authToken : "", sizeof(m_authToken) - 1);
    m_deviceUid[sizeof(m_deviceUid) - 1] = '\0';
    m_deviceName[sizeof(m_deviceName) - 1] = '\0';
    m_authToken[sizeof(m_authToken) - 1] = '\0';
    m_activeUtteranceId[0] = '\0';
    m_activeSessionId[0] = '\0';
    m_nextChunkSeq = 0;
}

void AudioUploadService::setStateMachine(UtteranceStateMachine* stateMachine) {
    m_stateMachine = stateMachine;
}

void AudioUploadService::freeJobPcm(UploadJob& job) {
    if (job.pcm != nullptr) {
        heap_caps_free(job.pcm);
        job.pcm = nullptr;
    }
}

void AudioUploadService::resetBatch() {
    m_batchLen = 0;
}

bool AudioUploadService::start() {
    if (m_running) {
        return true;
    }

    if (m_batchBuffer == nullptr) {
        m_batchBuffer = static_cast<uint8_t*>(
            heap_caps_malloc(audio::kUploadBatchBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (m_batchBuffer == nullptr) {
            ESP_LOGE(kTag, "batch buffer PSRAM alloc failed");
            return false;
        }
    }
    resetBatch();

    m_queue = xQueueCreate(audio::kUploadQueueDepth, sizeof(UploadJob));
    if (m_queue == nullptr) {
        ESP_LOGE(kTag, "upload queue alloc failed job_bytes=%u", static_cast<unsigned>(sizeof(UploadJob)));
        return false;
    }

    m_running = true;
    const BaseType_t created = xTaskCreate(
        uploadTask,
        "audio_upload",
        7168,
        this,
        5,
        reinterpret_cast<TaskHandle_t*>(&m_taskHandle));
    if (created != pdPASS) {
        m_running = false;
        vQueueDelete(static_cast<QueueHandle_t>(m_queue));
        m_queue = nullptr;
        ESP_LOGE(
            kTag,
            "upload task create failed heap_int=%lu",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        return false;
    }

    ESP_LOGI(
        kTag,
        "upload worker started batch_frames=%u",
        static_cast<unsigned>(audio::kUploadBatchFrames));
    return true;
}

void AudioUploadService::stop() {
    m_running = false;
    if (m_taskHandle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(static_cast<TaskHandle_t>(m_taskHandle));
        m_taskHandle = nullptr;
    }
    if (m_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(m_queue));
        m_queue = nullptr;
    }
    if (m_speechHttpClient != nullptr) {
        esp_http_client_cleanup(static_cast<esp_http_client_handle_t>(m_speechHttpClient));
        m_speechHttpClient = nullptr;
    }
    if (m_finalizeHttpClient != nullptr) {
        esp_http_client_cleanup(static_cast<esp_http_client_handle_t>(m_finalizeHttpClient));
        m_finalizeHttpClient = nullptr;
    }
    if (m_batchBuffer != nullptr) {
        heap_caps_free(m_batchBuffer);
        m_batchBuffer = nullptr;
    }
    resetBatch();
}

bool AudioUploadService::isConfigured() const {
    return isUrlConfigured(m_callbacks.speechUrl) && isUrlConfigured(m_callbacks.speechFinalizeUrl);
}

bool AudioUploadService::isRunning() const {
    return m_running && m_taskHandle != nullptr;
}

uint32_t AudioUploadService::postsOkCount() const {
    return m_postsOk;
}

uint32_t AudioUploadService::postsFailCount() const {
    return m_postsFail;
}

uint32_t AudioUploadService::chunksQueuedCount() const {
    return m_chunksQueued;
}

uint32_t AudioUploadService::chunksDroppedCount() const {
    return m_chunksDropped;
}

bool AudioUploadService::pingServer() {
    if (!isConfigured()) {
        ESP_LOGW(kTag, "ping: speech callbacks not configured");
        return false;
    }

    uint8_t silence[audio::kBytesPerFrame] = {};
    AudioProtocol protocol;
    if (!protocol.buildSpeechChunk(
            m_deviceUid,
            m_deviceName,
            "ping_test",
            m_activeSessionId[0] != '\0' ? m_activeSessionId : "ping_sess",
            0,
            m_audio.sampleRateHz,
            m_audio.channels,
            silence,
            sizeof(silence),
            m_postBody,
            sizeof(m_postBody))) {
        ESP_LOGW(kTag, "ping: failed to build chunk JSON");
        return false;
    }

    ESP_LOGI(kTag, "ping: POST %s", m_callbacks.speechUrl);
    return postJson(m_callbacks.speechUrl, m_postBody);
}

bool AudioUploadService::postJson(const char* url, const char* body) {
    if (!isUrlConfigured(url) || body == nullptr) {
        return false;
    }

    esp_http_client_handle_t client = makeHttpClient(url, 5000);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (m_authToken[0] != '\0') {
        esp_http_client_set_header(client, "X-Auth-Token", m_authToken);
    }
    esp_http_client_set_post_field(client, body, static_cast<int>(strlen(body)));

    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ++m_postsFail;
        ESP_LOGW(kTag, "HTTP POST failed: %s url=%s", esp_err_to_name(err), url);
        return false;
    }
    if (status < 200 || status >= 300) {
        ++m_postsFail;
        ESP_LOGW(kTag, "HTTP POST status=%d url=%s", status, url);
        return false;
    }
    ++m_postsOk;
    return true;
}

bool AudioUploadService::postSpeechJson(const char* body) {
    if (!isUrlConfigured(m_callbacks.speechUrl) || body == nullptr) {
        return false;
    }

    if (m_speechHttpClient == nullptr) {
        m_speechHttpClient = makeHttpClient(m_callbacks.speechUrl, 3000);
        if (m_speechHttpClient == nullptr) {
            return false;
        }
    }

    auto* client = static_cast<esp_http_client_handle_t>(m_speechHttpClient);
    esp_http_client_set_url(client, m_callbacks.speechUrl);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (m_authToken[0] != '\0') {
        esp_http_client_set_header(client, "X-Auth-Token", m_authToken);
    }
    esp_http_client_set_post_field(client, body, static_cast<int>(strlen(body)));

    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        ++m_postsFail;
        esp_http_client_cleanup(client);
        m_speechHttpClient = nullptr;
        ESP_LOGW(kTag, "speech POST failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status < 200 || status >= 300) {
        ++m_postsFail;
        ESP_LOGW(kTag, "speech POST status=%d", status);
        return false;
    }
    ++m_postsOk;
    return true;
}

bool AudioUploadService::postFinalizeJson(const char* body) {
    if (!isUrlConfigured(m_callbacks.speechFinalizeUrl) || body == nullptr) {
        return false;
    }

    if (m_finalizeHttpClient == nullptr) {
        m_finalizeHttpClient = makeHttpClient(m_callbacks.speechFinalizeUrl, 3000);
        if (m_finalizeHttpClient == nullptr) {
            return false;
        }
    }

    auto* client = static_cast<esp_http_client_handle_t>(m_finalizeHttpClient);
    esp_http_client_set_url(client, m_callbacks.speechFinalizeUrl);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (m_authToken[0] != '\0') {
        esp_http_client_set_header(client, "X-Auth-Token", m_authToken);
    }
    esp_http_client_set_post_field(client, body, static_cast<int>(strlen(body)));

    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        ++m_postsFail;
        esp_http_client_cleanup(client);
        m_finalizeHttpClient = nullptr;
        ESP_LOGW(kTag, "finalize POST failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status < 200 || status >= 300) {
        ++m_postsFail;
        ESP_LOGW(kTag, "finalize POST status=%d", status);
        return false;
    }
    ++m_postsOk;
    return true;
}

bool AudioUploadService::sendChunkJob(UploadJob& job) {
    AudioProtocol protocol;
    if (job.pcm == nullptr || job.pcmLen == 0) {
        freeJobPcm(job);
        return false;
    }

    if (!protocol.buildSpeechChunk(
            m_deviceUid,
            m_deviceName,
            job.utteranceId,
            job.sessionId,
            job.chunkSeq,
            m_audio.sampleRateHz,
            m_audio.channels,
            job.pcm,
            job.pcmLen,
            m_postBody,
            sizeof(m_postBody))) {
        ESP_LOGW(kTag, "chunk JSON too large seq=%lu pcm=%u", static_cast<unsigned long>(job.chunkSeq),
                 static_cast<unsigned>(job.pcmLen));
        freeJobPcm(job);
        return false;
    }

    const bool ok = postSpeechJson(m_postBody);
    freeJobPcm(job);

    if (!ok) {
        return false;
    }

    if (m_stateMachine != nullptr) {
        m_stateMachine->onChunkUploaded();
    }
    if ((job.chunkSeq % 5U) == 0U) {
        ESP_LOGI(
            kTag,
            "chunk uploaded id=%s seq=%lu pcm_bytes=%u ok=%lu dropped=%lu",
            job.utteranceId,
            static_cast<unsigned long>(job.chunkSeq),
            static_cast<unsigned>(job.pcmLen),
            static_cast<unsigned long>(m_postsOk),
            static_cast<unsigned long>(m_chunksDropped));
    }
    return true;
}

bool AudioUploadService::sendFinalizeJob(const UploadJob& job) {
    AudioProtocol protocol;
    char finalizeBody[512] = {};
    if (!protocol.buildSpeechFinalize(
            m_deviceUid,
            m_deviceName,
            job.utteranceId,
            job.sessionId,
            job.chunkCount,
            job.durationMs,
            finalizeBody,
            sizeof(finalizeBody))) {
        return false;
    }
    const bool ok = postFinalizeJson(finalizeBody);
    if (ok) {
        ESP_LOGI(
            kTag,
            "finalize uploaded id=%s chunks=%lu duration_ms=%lu",
            job.utteranceId,
            static_cast<unsigned long>(job.chunkCount),
            static_cast<unsigned long>(job.durationMs));
    }
    return ok;
}

void AudioUploadService::uploadTask(void* arg) {
    auto* self = static_cast<AudioUploadService*>(arg);
    if (self != nullptr) {
        self->runUploadLoop();
    }
    vTaskDelete(nullptr);
}

void AudioUploadService::runUploadLoop() {
    UploadJob job = {};
    while (m_running) {
        if (xQueueReceive(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }

        if (job.type == JobType::Chunk) {
            sendChunkJob(job);
        } else {
            sendFinalizeJob(job);
            m_activeUtteranceId[0] = '\0';
            m_activeSessionId[0] = '\0';
            m_nextChunkSeq = 0;
            resetBatch();
        }
    }
}

bool AudioUploadService::startUtterance(const char* utteranceId, const char* sessionId) {
    if (!isConfigured()) {
        ESP_LOGW(kTag, "speech callbacks not configured");
        return false;
    }
    if (utteranceId == nullptr || sessionId == nullptr) {
        return false;
    }

    strncpy(m_activeUtteranceId, utteranceId, sizeof(m_activeUtteranceId) - 1);
    strncpy(m_activeSessionId, sessionId, sizeof(m_activeSessionId) - 1);
    m_activeUtteranceId[sizeof(m_activeUtteranceId) - 1] = '\0';
    m_activeSessionId[sizeof(m_activeSessionId) - 1] = '\0';
    m_nextChunkSeq = 0;
    resetBatch();
    ESP_LOGI(kTag, "utterance upload armed id=%s", utteranceId);
    return true;
}

bool AudioUploadService::queuePcmJob(const char* utteranceId, const char* sessionId, const uint8_t* pcm, size_t pcmLen) {
    if (pcm == nullptr || pcmLen == 0 || m_queue == nullptr) {
        return false;
    }

    auto* pcmCopy = static_cast<uint8_t*>(heap_caps_malloc(pcmLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pcmCopy == nullptr) {
        ESP_LOGW(kTag, "chunk PSRAM alloc failed (%u bytes)", static_cast<unsigned>(pcmLen));
        return false;
    }
    memcpy(pcmCopy, pcm, pcmLen);

    UploadJob job = {};
    job.type = JobType::Chunk;
    strncpy(job.utteranceId, utteranceId != nullptr ? utteranceId : "", sizeof(job.utteranceId) - 1);
    strncpy(job.sessionId, sessionId != nullptr ? sessionId : "", sizeof(job.sessionId) - 1);
    job.chunkSeq = m_nextChunkSeq++;
    job.pcmLen = pcmLen;
    job.pcm = pcmCopy;

    if (xQueueSend(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(50)) != pdTRUE) {
        freeJobPcm(job);
        ++m_chunksDropped;
        ESP_LOGW(kTag, "upload queue full, dropping chunk");
        return false;
    }
    ++m_chunksQueued;
    return true;
}

bool AudioUploadService::flushBatch(const char* utteranceId, const char* sessionId) {
    if (m_batchLen == 0 || m_batchBuffer == nullptr) {
        return true;
    }
    const bool ok = queuePcmJob(utteranceId, sessionId, m_batchBuffer, m_batchLen);
    resetBatch();
    return ok;
}

bool AudioUploadService::uploadChunk(const char* utteranceId, const char* sessionId, const uint8_t* pcm, size_t pcmLen) {
    if (!m_running || !isConfigured() || pcm == nullptr || pcmLen == 0 || m_batchBuffer == nullptr) {
        return false;
    }
    if (pcmLen > audio::kBytesPerFrame) {
        return false;
    }
    if (m_batchLen + pcmLen > audio::kUploadBatchBytes) {
        if (!flushBatch(utteranceId, sessionId)) {
            return false;
        }
    }

    memcpy(m_batchBuffer + m_batchLen, pcm, pcmLen);
    m_batchLen += pcmLen;

    if (m_batchLen >= audio::kUploadBatchBytes) {
        return flushBatch(utteranceId, sessionId);
    }
    return true;
}

bool AudioUploadService::finalizeUtterance(
    const char* utteranceId,
    const char* sessionId,
    uint32_t chunkCount,
    uint32_t durationMs) {
    if (!m_running || !isConfigured() || m_queue == nullptr) {
        return false;
    }

    flushBatch(utteranceId, sessionId);

    UploadJob job = {};
    job.type = JobType::Finalize;
    strncpy(job.utteranceId, utteranceId != nullptr ? utteranceId : "", sizeof(job.utteranceId) - 1);
    strncpy(job.sessionId, sessionId != nullptr ? sessionId : "", sizeof(job.sessionId) - 1);
    job.chunkCount = chunkCount;
    job.durationMs = durationMs;
    job.pcm = nullptr;

    if (xQueueSend(static_cast<QueueHandle_t>(m_queue), &job, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(kTag, "failed to queue finalize");
        return false;
    }
    return true;
}
