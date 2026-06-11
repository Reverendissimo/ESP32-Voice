        /**
         * @file device_identity.hpp
         * @brief Derives immutable device identity from hardware.
         *
         * Responsibilities:
 * - produce stable device_uid
 * - validate inbound target_device_uid
         *
         * Non-responsibilities:
 * - config storage
 * - HTTP handling
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Derives immutable device identity from hardware.
         */
        class DeviceIdentity {
        public:
            /**
     * @brief Loads hardware identity.
     */
    /**
     * @brief Returns immutable device UID.
     */
    /**
     * @brief Returns true when target matches local UID.
 * @param targetDeviceUid const char*.
     */
    bool initialize();
    const char* deviceUid();
    bool matchesTarget(const char* targetDeviceUid);
        };
