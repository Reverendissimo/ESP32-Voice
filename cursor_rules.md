# Cursor Rules

## Role

You are implementing production firmware for **ESP32-S3-BOX-3 + SENSOR dock** with **ESP-IDF**.

Treat this document as the highest-priority implementation directive for the repository.

## Objective

Build a voice terminal, not an on-device assistant.

The firmware must:
- detect presence with radar when enabled
- detect speech with VAD
- stream audio to a server
- receive async playback commands
- receive async display commands
- expose config, diagnostics, sensors, and IR over REST
- support local maintenance over plain USB serial terminal
- support multiple devices in the same house safely via stable device identity

## Must-follow rules

### Architecture
- Use ESP-IDF.
- Use C++.
- Keep the design modular.
- One class per file.
- One responsibility per class.
- Keep files small.
- Do not create god objects.
- Do not centralize unrelated logic into one manager.
- Do not mix HTTP request parsing with hardware drivers.
- Do not mix config persistence with UI, CLI, or REST parsing.

### Documentation and comments
- Every non-trivial file must begin with a file header doc comment.
- Every class must have a docstring comment.
- Every public method must have a docstring comment.
- Comment intent, constraints, timing assumptions, and hardware quirks.
- Never leave non-obvious behavior undocumented.

### Naming and files
- File names must clearly reflect responsibility.
- One class = one file pair when appropriate.
- Avoid generic names such as `utils`, `helpers`, `misc`, `common` unless strictly scoped and justified.
- Split large files before they become hard to navigate.

### Config
- Maintain three config layers: defaults, saved, active.
- Active config lives in RAM.
- Saved config lives in NVS.
- Changes must not be persisted automatically.
- Save only on explicit save action.
- Wi-Fi credentials belong to config.
- Alarm thresholds belong to config.
- Trigger and clear thresholds or equivalent hysteresis must be explicit.

### Device identity
- Derive immutable `device_uid` from hardware identity.
- Keep editable `device_name` separate.
- Include identity fields in REST traffic.
- Reject commands addressed to another device.

### Networking and security
- No OTA.
- Add auth token support for control/admin endpoints.
- Never log secrets.
- Add request IDs and command IDs.
- Use API versioning.

### Display
- Use declarative JSON screen schema.
- No HTML.
- No browser engine assumptions.
- First version uses simple full-screen replacement.

### Audio
- VAD starts upload.
- Silence timeout ends utterance conservatively.
- Playback is async and independent from speech upload response.

### USB CLI
- Must work in a normal serial terminal.
- Keep commands human-readable.
- Do not require JSON input.

## Required source-of-truth documentation

Use these docs instead of guessing:

- ESP-IDF HTTP client: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_client.html
- ESP-IDF NVS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
- ESP-IDF system time: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html
- ESP-IDF logging: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/log.html
- ESP-IDF console: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/console.html
- ESP-IDF USB serial/JTAG console: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html
- ESP-IDF unit tests with Unity: https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/unit-testing.html
- ESP-IDF tests with Pytest: https://documentation.espressif.com/projects/esp-idf/en/latest/esp32/contribute/esp-idf-tests-with-pytest.html
- ESP-IDF Linux host testing: https://docs.espressif.com/projects/esp-idf/en/v5.0.4/esp32/api-guides/linux-host-testing.html
- BOX-3 hardware overview: https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md
- BOX-3 getting started: https://documentation.espressif.com/esp-box/master/docs/getting_started.md

## Required work mode

When implementing:
- work incrementally
- keep the project compiling at each stage
- add tests with the code, not afterwards
- update docs when behavior changes
- do not invent hidden behavior not covered by config or code comments

## Deliverable discipline

For every new class:
- create file header doc comment
- create class docstring
- create public method docstrings
- add tests
- keep responsibility narrow

## Stop conditions

Stop and reconsider if:
- a file grows into multiple responsibilities
- a route handler contains business logic
- config validation is duplicated in more than one place
- device identity checks are missing from inbound control paths
- alarm thresholds are hardcoded outside config
