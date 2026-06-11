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

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Hardware-derived device identity.
 */
class DeviceIdentity {
public:
    /**
     * @brief Loads hardware identity from Wi-Fi station MAC.
     *
     * @return true when device_uid was derived successfully.
     */
    bool initialize();

    /**
     * @brief Returns immutable device UID (e.g. espbox-aabbccddeeff).
     */
    const char* deviceUid() const;

    /**
     * @brief Returns true when target matches local device UID.
     *
     * @param targetDeviceUid Expected inbound command target.
     */
    bool matchesTarget(const char* targetDeviceUid) const;

private:
    char m_deviceUid[32] = {};
    bool m_initialized = false;
};
