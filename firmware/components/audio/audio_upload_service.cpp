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

constexpr int kStreamTimeoutMs = 60000;
constexpr int kFinalizeTimeoutMs = 5000;

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
    config.buffer_size_tx = 4096;
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
        "upload worker started protocol=A3 batch_frames=%u",
        static_cast<unsigned>(audio::kUploadBatchFrames));
    return true;
}

void AudioUploadService::stop() {
    m_running = false;
    if (m_streamOpen) {
        closeStream();
    }
    if (m_taskHandle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(static_cast<TaskHandle_t>(m_taskHandle));
        m_taskHandle = nullptr;
    }
    if (m_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(m_queue));
        m_queue = nullptr;
    }
    if (m_streamClient != nullptr) {
        esp_http_client_cleanup(static_cast<esp_http_client_handle_t>(m_streamClient));
        m_streamClient = nullptr;
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
    m_streamOpen = false;
    m_streamBytesWritten = 0;
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

bool AudioUploadService::openStream(const char* utteranceId, const char* sessionId) {
    if (!isUrlConfigured(m_callbacks.speechUrl) || utteranceId == nullptr || sessionId == nullptr) {
        return false;
    }
    if (m_streamOpen) {
        closeStream();
    }

    m_streamClient = makeHttpClient(m_callbacks.speechUrl, kStreamTimeoutMs);
    if (m_streamClient == nullptr) {
        return false;
    }

    auto* client = static_cast<esp_http_client_handle_t>(m_streamClient);
    esp_http_client_set_url(client, m_callbacks.speechUrl);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "X-Protocol", "A3");
    esp_http_client_set_header(client, "X-Device-Uid", m_deviceUid);
    esp_http_client_set_header(client, "X-Device-Name", m_deviceName);
    esp_http_client_set_header(client, "X-Utterance-Id", utteranceId);
    esp_http_client_set_header(client, "X-Session-Id", sessionId);
    if (m_authToken[0] != '\0') {
        esp_http_client_set_header(client, "X-Auth-Token", m_authToken);
    }

    char headerValue[16] = {};
    snprintf(headerValue, sizeof(headerValue), "%u", static_cast<unsigned>(m_audio.sampleRateHz));
    esp_http_client_set_header(client, "X-Sample-Rate", headerValue);
    snprintf(headerValue, sizeof(headerValue), "%u", static_cast<unsigned>(m_audio.channels));
    esp_http_client_set_header(client, "X-Channels", headerValue);
    esp_http_client_set_header(client, "X-Sample-Format", "s16le");

    const esp_err_t err = esp_http_client_open(client, -1);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "stream open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        m_streamClient = nullptr;
        return false;
    }

    m_streamOpen = true;
    m_streamBytesWritten = 0;
    ESP_LOGI(kTag, "stream opened id=%s url=%s", utteranceId, m_callbacks.speechUrl);
    return true;
}

bool AudioUploadService::writeRaw(const uint8_t* data, size_t len) {
    if (!m_streamOpen || m_streamClient == nullptr || data == nullptr || len == 0) {
        return false;
    }

    auto* client = static_cast<esp_http_client_handle_t>(m_streamClient);
    size_t offset = 0;
    while (offset < len) {
        const int written = esp_http_client_write(
            client,
            reinterpret_cast<const char*>(data + offset),
            static_cast<int>(len - offset));
        if (written <= 0) {
            ESP_LOGW(
                kTag,
                "stream raw write failed at offset=%u/%u",
                static_cast<unsigned>(offset),
                static_cast<unsigned>(len));
            return false;
        }
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool AudioUploadService::writeStream(const uint8_t* pcm, size_t pcmLen) {
    if (!m_streamOpen || m_streamClient == nullptr || pcm == nullptr || pcmLen == 0) {
        return false;
    }

    // esp_http_client_open(-1) sets Transfer-Encoding: chunked but does not frame writes.
    char chunkHeader[16] = {};
    const int headerLen = snprintf(chunkHeader, sizeof(chunkHeader), "%x\r\n", static_cast<unsigned>(pcmLen));
    if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(chunkHeader)) {
        return false;
    }

    if (!writeRaw(reinterpret_cast<const uint8_t*>(chunkHeader), static_cast<size_t>(headerLen))) {
        return false;
    }
    if (!writeRaw(pcm, pcmLen)) {
        return false;
    }
    static const uint8_t kChunkEnd[] = {'\r', '\n'};
    if (!writeRaw(kChunkEnd, sizeof(kChunkEnd))) {
        return false;
    }

    m_streamBytesWritten += static_cast<uint32_t>(pcmLen);
    return true;
}

void AudioUploadService::abortStream() {
    if (!m_streamOpen || m_streamClient == nullptr) {
        m_streamOpen = false;
        m_streamBytesWritten = 0;
        return;
    }

    auto* client = static_cast<esp_http_client_handle_t>(m_streamClient);
    static const char kChunkTerminator[] = "0\r\n\r\n";
    writeRaw(reinterpret_cast<const uint8_t*>(kChunkTerminator), sizeof(kChunkTerminator) - 1);
    esp_http_client_fetch_headers(client);
    esp_http_client_cleanup(client);
    m_streamClient = nullptr;
    m_streamOpen = false;
    m_streamBytesWritten = 0;
}

bool AudioUploadService::closeStream() {
    if (!m_streamOpen || m_streamClient == nullptr) {
        m_streamOpen = false;
        return true;
    }

    auto* client = static_cast<esp_http_client_handle_t>(m_streamClient);
    const uint32_t bytesWritten = m_streamBytesWritten;
    static const char kChunkTerminator[] = "0\r\n\r\n";
    if (!writeRaw(reinterpret_cast<const uint8_t*>(kChunkTerminator), sizeof(kChunkTerminator) - 1)) {
        abortStream();
        ++m_postsFail;
        ESP_LOGW(kTag, "stream terminator write failed bytes=%lu", static_cast<unsigned long>(bytesWritten));
        return false;
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        abortStream();
        ++m_postsFail;
        ESP_LOGW(kTag, "stream response headers failed bytes=%lu", static_cast<unsigned long>(bytesWritten));
        return false;
    }

    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    m_streamClient = nullptr;
    m_streamOpen = false;
    m_streamBytesWritten = 0;

    if (status < 200 || status >= 300) {
        ++m_postsFail;
        ESP_LOGW(kTag, "stream close status=%d bytes=%lu", status, static_cast<unsigned long>(bytesWritten));
        return false;
    }

    ++m_postsOk;
    ESP_LOGI(kTag, "stream closed status=%d bytes=%lu", status, static_cast<unsigned long>(bytesWritten));
    return true;
}

bool AudioUploadService::pingServer() {
    if (!isConfigured()) {
        ESP_LOGW(kTag, "ping: speech callbacks not configured");
        return false;
    }

    uint8_t silence[audio::kBytesPerFrame] = {};
    ESP_LOGI(kTag, "ping: POST stream %s", m_callbacks.speechUrl);
    if (!openStream("ping_test", "ping_sess")) {
        return false;
    }
    const bool wrote = writeStream(silence, sizeof(silence));
    const bool ok = wrote && closeStream();
    if (!ok) {
        ESP_LOGW(kTag, "ping: stream failed");
    }
    return ok;
}

bool AudioUploadService::postFinalizeJson(const char* body) {
    if (!isUrlConfigured(m_callbacks.speechFinalizeUrl) || body == nullptr) {
        return false;
    }

    if (m_finalizeHttpClient == nullptr) {
        m_finalizeHttpClient = makeHttpClient(m_callbacks.speechFinalizeUrl, kFinalizeTimeoutMs);
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
    if (job.pcm == nullptr || job.pcmLen == 0) {
        freeJobPcm(job);
        return false;
    }
    if (m_streamBroken) {
        freeJobPcm(job);
        return false;
    }

    if (!m_streamOpen) {
        if (!openStream(job.utteranceId, job.sessionId)) {
            freeJobPcm(job);
            return false;
        }
    }

    const bool ok = writeStream(job.pcm, job.pcmLen);
    freeJobPcm(job);

    if (!ok) {
        m_streamBroken = true;
        abortStream();
        return false;
    }

    if (m_stateMachine != nullptr) {
        m_stateMachine->onChunkUploaded();
    }
    if ((job.chunkSeq % 5U) == 0U) {
        ESP_LOGI(
            kTag,
            "stream write id=%s seq=%lu pcm_bytes=%u total=%lu ok=%lu dropped=%lu",
            job.utteranceId,
            static_cast<unsigned long>(job.chunkSeq),
            static_cast<unsigned>(job.pcmLen),
            static_cast<unsigned long>(m_streamBytesWritten),
            static_cast<unsigned long>(m_postsOk),
            static_cast<unsigned long>(m_chunksDropped));
    }
    return true;
}

bool AudioUploadService::sendFinalizeJob(const UploadJob& job) {
    if (m_streamOpen) {
        if (!closeStream()) {
            ESP_LOGW(kTag, "stream close failed before finalize id=%s", job.utteranceId);
        }
    }

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
            m_streamBroken = false;
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
    m_streamBroken = false;
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
