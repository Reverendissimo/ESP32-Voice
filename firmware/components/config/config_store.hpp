/**
 * @file config_store.hpp
 * @brief Persists saved configuration in NVS.
 *
 * Responsibilities:
 * - load and save config blobs
 *
 * Non-responsibilities:
 * - RAM active config management
 * - validation rules
 */
#pragma once

#include <stdbool.h>

#include "config_models.hpp"

/**
 * @brief NVS-backed saved configuration storage.
 */
class ConfigStore {
public:
    /**
     * @brief Loads saved configuration from NVS into outConfig.
     *
     * @return true when saved config exists and was parsed.
     */
    bool loadSaved(config::AppConfig& outConfig) const;

    /**
     * @brief Persists configuration to NVS.
     */
    bool save(const config::AppConfig& config) const;

    /**
     * @brief Removes saved configuration from NVS.
     */
    bool clearSaved() const;
};
