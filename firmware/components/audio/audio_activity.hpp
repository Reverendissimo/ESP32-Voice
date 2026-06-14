/**
 * @file audio_activity.hpp
 * @brief Optional UI hooks for capture/playback lifecycle (no display dependency).
 */
#pragma once

#include <stdint.h>

enum class AudioActivity : uint8_t {
    Listening,
    Recording,
    Speaking,
    MicOff,
};

using AudioActivityFn = void (*)(void* ctx, AudioActivity activity);

struct AudioActivityCallbacks {
    AudioActivityFn onActivity = nullptr;
    void* context = nullptr;
};
