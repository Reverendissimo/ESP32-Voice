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

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Persists saved configuration in NVS.
         */
        class ConfigStore {
        public:
            /**
     * @brief Loads saved config from NVS.
     */
    /**
     * @brief Persists active config to NVS.
     */
    bool loadSaved();
    bool saveActive();
        };
