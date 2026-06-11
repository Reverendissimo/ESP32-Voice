        /**
         * @file environment_service.hpp
         * @brief Reads temperature and humidity sensors.
         *
         * Responsibilities:
 * - sample and expose environment values
         *
         * Non-responsibilities:
 * - alarm threshold logic
 * - REST parsing
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Reads temperature and humidity sensors.
         */
        class EnvironmentService {
        public:
            /**
     * @brief Starts environment sampling.
     */
    bool start();
        };
