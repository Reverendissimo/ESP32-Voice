/**
 * @file config_validator.hpp
 * @brief Validates config patches and full config objects.
 *
 * Responsibilities:
 * - reject invalid ranges and missing required fields
 *
 * Non-responsibilities:
 * - NVS I/O
 * - REST parsing
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config_models.hpp"

struct cJSON;

/**
 * @brief Configuration validation helpers.
 */
class ConfigValidator {
public:
    /**
     * @brief Validates a complete configuration object.
     *
     * @param config Candidate configuration.
     * @param errorOut Optional short error message buffer.
     * @param errorOutLen Length of errorOut.
     * @return true when configuration is valid.
     */
    bool validateConfig(const config::AppConfig& config, char* errorOut, size_t errorOutLen) const;

    /**
     * @brief Validates a JSON patch object before merge.
     *
     * @param patch Parsed JSON patch object.
     * @param errorOut Optional short error message buffer.
     * @param errorOutLen Length of errorOut.
     * @return true when patch shape is valid.
     */
    bool validatePatch(const cJSON* patch, char* errorOut, size_t errorOutLen) const;
};
