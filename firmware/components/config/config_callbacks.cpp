/**
 * @file config_callbacks.cpp
 * @brief Implementation of callback URL resolution.
 */
#include "config_callbacks.hpp"

#include <stdio.h>
#include <string.h>

namespace config {

namespace {

void copyString(char* dest, size_t destSize, const char* value) {
    if (value == nullptr) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, destSize - 1);
    dest[destSize - 1] = '\0';
}

void appendPath(char* dest, size_t destSize, const char* base, const char* path) {
    if (base == nullptr || path == nullptr || destSize == 0) {
        return;
    }

    size_t baseLen = strlen(base);
    while (baseLen > 0 && base[baseLen - 1] == '/') {
        --baseLen;
    }

    if (path[0] == '/') {
        snprintf(dest, destSize, "%.*s%s", static_cast<int>(baseLen), base, path);
    } else {
        snprintf(dest, destSize, "%.*s/%s", static_cast<int>(baseLen), base, path);
    }
}

}  // namespace

void resolveCallbacks(const AppConfig& config, CallbacksConfig& out) {
    out = config.callbacks;

    if (out.speechUrl[0] == '\0' && config.network.callbackBaseUrl[0] != '\0') {
        appendPath(out.speechUrl, sizeof(out.speechUrl), config.network.callbackBaseUrl, "/speech");
    }
    if (out.speechFinalizeUrl[0] == '\0' && config.network.callbackBaseUrl[0] != '\0') {
        appendPath(out.speechFinalizeUrl, sizeof(out.speechFinalizeUrl), config.network.callbackBaseUrl, "/speech/finalize");
    }
}

}  // namespace config
