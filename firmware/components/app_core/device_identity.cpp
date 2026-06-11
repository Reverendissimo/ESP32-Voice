        /**
         * @file device_identity.cpp
         * @brief Implementation of DeviceIdentity.
         */
        #include "device_identity.hpp"

        bool DeviceIdentity::initialize() {
    return false;
}

const char* DeviceIdentity::deviceUid() {
    return "";
}

bool DeviceIdentity::matchesTarget(const char* targetDeviceUid) {
    return false;
}
