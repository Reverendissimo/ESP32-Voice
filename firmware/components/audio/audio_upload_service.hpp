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
    bool ensureStreamOpen(const char* utteranceId, const char* sessionId);
    bool uploadChunk(
        const char* utteranceId,
        const char* sessionId,
        const uint8_t* pcm,
        size_t pcmLen,
        bool reliable = false);
    bool flushPendingBatch(const char* utteranceId, const char* sessionId, bool reliable = false);
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
    uint32_t chunksDroppedCount() const;

private:
    enum class JobType : uint8_t {
        OpenStream,
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
        int8_t poolSlot;
    };

    void freeJobPcm(UploadJob& job);
    static void uploadTask(void* arg);
    void runUploadLoop();
    bool postFinalizeJson(const char* body);
    bool tryQueuePcmJob(const char* utteranceId, const char* sessionId, const uint8_t* pcm, size_t pcmLen);
    bool queuePcmJob(const char* utteranceId, const char* sessionId, const uint8_t* pcm, size_t pcmLen);
    bool queuePcmJobReliable(const char* utteranceId, const char* sessionId, const uint8_t* pcm, size_t pcmLen);
    bool flushBatch(const char* utteranceId, const char* sessionId, bool reliable = false);
    int acquirePoolSlot();
    void releasePoolSlot(int slot);
    uint8_t* poolSlotData(int slot);
    bool waitForWifiHeap();
    bool openStream(const char* utteranceId, const char* sessionId);
    bool writeRaw(const uint8_t* data, size_t len);
    bool writeStream(const uint8_t* pcm, size_t pcmLen);
    bool closeStream();
    void abortStream();
    bool sendOpenStreamJob(const UploadJob& job);
    bool sendChunkJob(UploadJob& job);
    bool sendFinalizeJob(const UploadJob& job);
    void resetBatch();

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
    void* m_streamClient = nullptr;
    void* m_finalizeHttpClient = nullptr;
    bool m_streamOpen = false;
    bool m_streamBroken = false;
    uint32_t m_streamBytesWritten = 0;
    uint8_t* m_batchBuffer = nullptr;
    uint8_t* m_chunkPool = nullptr;
    bool m_slotUsed[audio::kUploadQueueDepth] = {};
    size_t m_batchLen = 0;
    uint32_t m_postsOk = 0;
    uint32_t m_postsFail = 0;
    uint32_t m_chunksQueued = 0;
    uint32_t m_chunksDropped = 0;
};
