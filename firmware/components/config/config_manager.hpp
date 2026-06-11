/**
 * @file config_manager.hpp
 * @brief Manages defaults, saved, and active config layers.
 *
 * Responsibilities:
 * - apply patches to active RAM config
 * - revert and explicit save
 *
 * Non-responsibilities:
 * - HTTP request parsing
 * - Wi-Fi driver control
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config_models.hpp"
#include "config_store.hpp"
#include "config_validator.hpp"

struct cJSON;

/**
 * @brief Runtime and persisted configuration state owner.
 */
class ConfigManager {
public:
    /**
     * @brief Initializes defaults, saved, and active config layers.
     */
    bool initialize();

    /**
     * @brief Applies a JSON patch to active RAM config.
     *
     * @param patchJson Raw JSON patch object string.
     * @param errorOut Optional validation error buffer.
     * @param errorOutLen Length of errorOut.
     */
    bool applyPatchJson(const char* patchJson, char* errorOut, size_t errorOutLen);

    /**
     * @brief Persists active config to NVS and updates saved layer.
     */
    bool saveActive();

    /**
     * @brief Reloads saved config into active RAM config.
     */
    bool loadSavedIntoActive();

    /**
     * @brief Discards unsaved active changes by restoring saved layer.
     */
    void revertActive();

    /**
     * @brief Restores active config to factory defaults in RAM only.
     */
    void resetActiveToDefaults();

    /**
     * @brief Clears saved config and resets active to defaults.
     */
    bool resetSaved();

    /**
     * @brief Returns true when active differs from saved.
     */
    bool isDirty() const;

    const config::AppConfig& defaults() const;
    const config::AppConfig& saved() const;
    const config::AppConfig& active() const;

private:
    bool configsEqual(const config::AppConfig& left, const config::AppConfig& right) const;
    bool applyPatchObject(const cJSON* patch, char* errorOut, size_t errorOutLen);
    void updateDirtyFlag();

    ConfigStore m_store;
    ConfigValidator m_validator;
    config::AppConfig m_defaults = {};
    config::AppConfig m_saved = {};
    config::AppConfig m_active = {};
    bool m_hasSaved = false;
    bool m_dirty = false;
};
