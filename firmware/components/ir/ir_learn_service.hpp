        /**
         * @file ir_learn_service.hpp
         * @brief Runs modal IR learning with timeout.
         *
         * Responsibilities:
 * - learn start, success, timeout, cancel
         *
         * Non-responsibilities:
 * - HTTP route parsing
 * - send path
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Runs modal IR learning with timeout.
         */
        class IrLearnService {
        public:
            /**
     * @brief Enters IR learning mode.
     */
    bool startLearn();
        };
