/**
 * @file config_callbacks.hpp
 * @brief Resolves outbound callback URLs from active config.
 */
#pragma once

#include <stddef.h>

#include "config_models.hpp"

namespace config {

/**
 * @brief Fills speech URLs from callbacks section, or derives them from network.callbackBaseUrl.
 */
void resolveCallbacks(const AppConfig& config, CallbacksConfig& out);

/**
 * @brief Resolve OTA manifest URL from explicit config or callback base URL.
 */
void resolveOtaManifestUrl(const AppConfig& config, char* out, size_t outLen);

}  // namespace config
