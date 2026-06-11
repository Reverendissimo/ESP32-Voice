        /**
         * @file app_bootstrap.hpp
         * @brief Owns firmware startup and subsystem wiring order.
         *
         * Responsibilities:
 * - initialize NVS and core services
 * - start configured subsystems
         *
         * Non-responsibilities:
 * - HTTP route business logic
 * - hardware driver details
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Owns firmware startup and subsystem wiring order.
         */
        class AppBootstrap {
        public:
            /**
     * @brief Starts platform services in dependency order.
     */
    bool start();
        };
