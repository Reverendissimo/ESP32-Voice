/**
 * @file json_response_builder.hpp
 * @brief Builds consistent JSON success responses.
 *
 * Responsibilities:
 * - include version and identity fields
 *
 * Non-responsibilities:
 * - business rule evaluation
 */
#pragma once

#include <stddef.h>

#include "config_models.hpp"
#include "health_service.hpp"

/**
 * @brief JSON response builder for REST endpoints.
 */
class JsonResponseBuilder {
public:
    /**
     * @brief Builds GET /health JSON payload.
     */
    bool buildHealth(const char* deviceUid, const HealthSnapshot& snapshot, char* outBuffer, size_t outBufferLen) const;

    /**
     * @brief Builds GET /version JSON payload.
     */
    bool buildVersion(const char* deviceUid, char* outBuffer, size_t outBufferLen) const;

    /**
     * @brief Builds GET /config JSON payload.
     */
    bool buildConfig(
        const char* deviceUid,
        const config::AppConfig& config,
        bool dirty,
        bool maskSecrets,
        char* outBuffer,
        size_t outBufferLen) const;
};
