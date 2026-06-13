/**
 * @file audio_capture_service.hpp
 * @brief Captures microphone audio frames.
 *
 * Responsibilities:
 * - start/stop capture pipeline
 * - feed VAD and upload pipeline
 *
 * Non-responsibilities:
 * - VAD decisions
 * - HTTP upload
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "config_models.hpp"

class Box3AudioBoard;
class VadService;
class UtteranceStateMachine;
class AudioUploadService;

/**
 * @brief Captures microphone audio frames.
 */
class AudioCaptureService {
public:
    /**
     * @brief Wires dependencies for the capture loop.
     */
    void configure(
        Box3AudioBoard* board,
        VadService* vad,
        UtteranceStateMachine* utteranceFsm,
        AudioUploadService* upload,
        const config::AudioConfig& audio,
        const config::VadConfig& vadConfig);

    /**
     * @brief Starts audio capture task.
     */
    bool start();

    /**
     * @brief Stops capture task and closes streams.
     */
    void stop();

    /**
     * @brief Stops capture and waits for the reader task to exit (for mic_probe).
     */
    bool stopGracefully(uint32_t timeoutMs);

    bool isRunning() const;

    uint32_t lastFrameEnergy() const;
    uint32_t peakFrameEnergy() const;
    uint32_t readFailCount() const;
    uint32_t framesOkCount() const;
    uint32_t maxSampleAbs() const;

private:
    static void captureTask(void* arg);
    void runCaptureLoop();

    Box3AudioBoard* m_board = nullptr;
    VadService* m_vad = nullptr;
    UtteranceStateMachine* m_utteranceFsm = nullptr;
    AudioUploadService* m_upload = nullptr;
    config::AudioConfig m_audio = {};
    config::VadConfig m_vadConfig = {};
    bool m_running = false;
    void* m_taskHandle = nullptr;
    uint32_t m_lastEnergy = 0;
    uint32_t m_peakEnergy = 0;
    uint32_t m_readFailCount = 0;
    uint32_t m_framesOkCount = 0;
    uint32_t m_maxSampleAbs = 0;
    uint8_t* m_readBuffer = nullptr;
    size_t m_readBufferSize = 0;
};
