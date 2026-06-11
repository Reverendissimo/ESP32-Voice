        /**
         * @file ir_send_service.hpp
         * @brief Transmits learned or raw IR codes.
         *
         * Responsibilities:
 * - validate target and emit IR
         *
         * Non-responsibilities:
 * - learning workflow
 * - REST parsing
         */
        #pragma once

#include <stdint.h>
#include <stdbool.h>

/**
         * @brief Transmits learned or raw IR codes.
         */
        class IrSendService {
        public:
            /**
     * @brief Transmits IR code payload.
     */
    bool send();
        };
