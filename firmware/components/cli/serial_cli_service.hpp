/**
 * @file serial_cli_service.hpp
 * @brief Owns USB serial CLI session lifecycle.
 */
#pragma once

#include <stdbool.h>

#include "cli_context.hpp"

/**
 * @brief USB serial interactive CLI service.
 */
class SerialCliService {
public:
    /**
     * @brief Initializes console and starts CLI REPL task.
     */
    bool start(const CliContext* context);

    /**
     * @brief Returns true when CLI task is running.
     */
    bool isRunning() const;

private:
    bool m_running = false;
};
