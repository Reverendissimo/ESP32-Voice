/**
 * @file config_callbacks.hpp
 * @brief Resolves outbound callback URLs from active config.
 */
#pragma once

#include "config_models.hpp"

namespace config {

/**
 * @brief Fills speech URLs from callbacks section, or derives them from network.callbackBaseUrl.
 */
void resolveCallbacks(const AppConfig& config, CallbacksConfig& out);

}  // namespace config
