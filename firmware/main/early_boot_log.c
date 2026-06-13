/**
 * @file early_boot_log.c
 * @brief Prints firmware version before app_main (first application output).
 */
#include "esp_app_desc.h"
#include "esp_rom_sys.h"

__attribute__((constructor)) static void esp32_voice_early_version_log(void) {
    const esp_app_desc_t* app = esp_app_get_description();
    if (app == NULL) {
        esp_rom_printf(
            "\n\n"
            "========================================\n"
            " ESP32-Voice (version unknown)\n"
            "========================================\n\n");
        return;
    }

    esp_rom_printf(
        "\n\n"
        "========================================\n"
        " ESP32-Voice firmware %s\n"
        " build %s %s\n"
        " ESP-IDF %s\n"
        "========================================\n\n",
        app->version,
        app->date,
        app->time,
        app->idf_ver);
}
