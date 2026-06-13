/**
 * @file audio_upload_service.hpp
 * @brief Streams utterance audio to backend server.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_types.hpp"
#include "config_models.hpp"

class UtteranceStateMachine;

/**
 * @brief Streams utterance audio to backend server.
 */
class AudioUploadService {
public:
    void configure(
        const config::CallbacksConfig& callbacks,
        const config::AudioConfig& audio,
        const char* deviceUid,
        const char* deviceName,
        const char* authToken);

    void setStateMachine(UtteranceStateMachine* stateMachine);

    bool start();
    void stop();

    bool startUtterance(const char* utteranceId, const char* sessionId);
    bool uploadChunk(const char* utteranceId, const char* sessionId, const uint8_t* pcm, size_t pcmLen);
    bool finalizeUtterance(
        const char* utteranceId,
        const char* sessionId,
        uint32_t chunkCount,
        uint32_t durationMs);

    bool isConfigured() const;
    bool isRunning() const;
    bool pingServer();
    uint32_t postsOkCount() const;
    uint32_t postsFailCount() const;
    uint32_t chunksQueuedCount() const;

private:
    enum class JobType : uint8_t {
        Chunk,
        Finalize,
    };

    struct UploadJob {
        JobType type;
        char utteranceId[48];
        char sessionId[48];
        uint32_t chunkSeq;
        uint32_t chunkCount;
        uint32_t durationMs;
        size_t pcmLen;
        uint8_t* pcm;
    };

    static void freeJobPcm(UploadJob& job);
    static void uploadTask(void* arg);
    void runUploadLoop();
    bool postJson(const char* url, const char* body);
    bool sendChunkJob(UploadJob& job);
    bool sendFinalizeJob(const UploadJob& job);

    config::CallbacksConfig m_callbacks;
    config::AudioConfig m_audio;
    char m_deviceUid[32];
    char m_deviceName[32];
    char m_authToken[64];
    char m_activeUtteranceId[48];
    char m_activeSessionId[48];
    uint32_t m_nextChunkSeq;
    UtteranceStateMachine* m_stateMachine = nullptr;
    bool m_running = false;
    void* m_taskHandle = nullptr;
    void* m_queue = nullptr;
    char m_postBody[8192] = {};
    uint32_t m_postsOk = 0;
    uint32_t m_postsFail = 0;
    uint32_t m_chunksQueued = 0;
};
