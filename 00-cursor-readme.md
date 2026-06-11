# Cursor Agent Instructions

## Mission

Implement custom ESP-IDF firmware for **ESP32-S3-BOX-3 + SENSOR dock** as a **voice terminal**.

This device is **not** the assistant brain.

The device must:
- detect presence with radar when enabled
- detect speech with VAD
- start streaming audio to the server when speech starts
- finalize the utterance conservatively after silence
- receive async playback commands from server
- receive async display commands from server
- expose config, sensors, diagnostics, and IR over REST
- support safe local setup over plain USB serial terminal

## Build priority

Implement in this order:

1. Platform skeleton
2. Config manager
3. Wi-Fi + time sync + identity
4. Local REST server
5. USB CLI
6. Audio capture/upload
7. Playback
8. Display
9. Sensors and alarms
10. IR learn/send
11. Diagnostics hardening

## Non-negotiable constraints

- Use **ESP-IDF**, not Arduino.
- Use **plain C++** in a disciplined embedded style.
- One class per file.
- File names must clearly describe responsibility.
- Keep files small.
- Separate subsystems cleanly.
- Every public class and public method must have a docstring-style comment.
- Every non-trivial file must start with a file header comment explaining purpose.
- No god classes.
- No giant `app_main.cpp`.
- No hidden global state unless explicitly justified.
- Runtime config must be edited in RAM first and persisted only on explicit save.
- Do not implement OTA.

## Required documentation references

Before coding, read and follow these ESP-IDF references:

- HTTP client: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_client.html
- HTTP server: https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server
- NVS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
- Console: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/console.html
- USB Serial/JTAG console: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html
- System time / SNTP: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html
- SNTP example: https://github.com/espressif/esp-idf/blob/master/examples/protocols/sntp/README.md
- Logging: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/log.html

Also use official BOX-3 hardware docs as hardware truth source:

- BOX-3 hardware overview: https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md
- BOX-3 getting started: https://documentation.espressif.com/esp-box/master/docs/getting_started.md

## Expected output from Cursor

Produce:
- clean project tree
- incremental compilable commits/steps
- no placeholder architecture
- no monolithic files
- concise inline docs
- a short developer README for build/flash/serial usage

## Required behavior when unsure

If hardware pin mapping or peripheral details are unclear, check the official BOX-3 docs before guessing.
