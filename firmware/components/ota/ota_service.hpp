/**
 * @file ota_service.hpp
 * @brief HTTP OTA firmware updates gated by config.ota.secret.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "config_models.hpp"

class AudioPlaybackService;
class UtteranceStateMachine;

enum class OtaState : uint8_t {
    Idle,
    Downloading,
    Rebooting,
    Failed,
};

/**
 * @brief Downloads firmware images and switches the OTA boot partition.
 */
class OtaService {
public:
    void configure(
        const config::OtaConfig& ota,
        const char* callbackBaseUrl,
        AudioPlaybackService* playback,
        UtteranceStateMachine* utteranceFsm);
    void refreshConfig(const config::OtaConfig& ota, const char* callbackBaseUrl);

    bool isEnabled() const;
    bool authorize(const char* providedSecret) const;
    OtaState state() const;
    uint8_t progressPercent() const;
    const char* lastError() const;
    const char* targetVersion() const;

    bool startUpdate(const char* firmwareUrl, bool force);
    bool isSystemBusy() const;

    static void validateRunningImageOnBoot();

private:
    static void updateTask(void* arg);
    void runUpdate(const char* firmwareUrl, bool force);
    bool resolveManifest(char* firmwareUrl, size_t firmwareUrlLen, char* versionOut, size_t versionOutLen);
    bool downloadAndInstall(const char* firmwareUrl);

    config::OtaConfig m_ota = {};
    char m_callbackBaseUrl[128] = {};
    AudioPlaybackService* m_playback = nullptr;
    UtteranceStateMachine* m_utteranceFsm = nullptr;
    OtaState m_state = OtaState::Idle;
    uint8_t m_progressPercent = 0;
    char m_lastError[96] = {};
    char m_targetVersion[32] = {};
    void* m_taskHandle = nullptr;
};
