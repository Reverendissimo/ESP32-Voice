/**
 * @file cli_command_registry.hpp
 * @brief Registers and dispatches CLI commands.
 */
#pragma once

#include <stdbool.h>

#include "cli_context.hpp"

/**
 * @brief Serial CLI command registration and dispatch.
 */
class CliCommandRegistry {
public:
    /**
     * @brief Registers all required CLI commands.
     */
    bool registerCommands(const CliContext* context) const;
};
