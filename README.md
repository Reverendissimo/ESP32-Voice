# ESP32-Voice

ESP-IDF firmware for the **ESP32-S3-BOX-3 + SENSOR dock** as a **voice terminal**.

The device is not the assistant brain. It detects presence and speech, streams audio to a backend server, and handles async playback, display, sensors, and IR control over REST. Local setup and recovery use a plain USB serial CLI.

## Hardware

- [ESP32-S3-BOX-3](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md)
- SENSOR dock (radar, environment sensors, IR)

## Features

| Area | Behavior |
|------|----------|
| Speech | VAD starts upload; conservative silence timeout ends utterance |
| Playback | Async `POST /play` from server |
| Display | Declarative JSON screens rendered with LVGL |
| Config | Defaults / saved (NVS) / active (RAM); save only on explicit action |
| Sensors | Battery, temperature/humidity, radar presence |
| IR | Learn and send with modal timeout |
| CLI | USB serial terminal for Wi-Fi, config, health, reboot |

## Architecture

```
Backend server  <──HTTP──>  ESP32-S3-BOX-3
                              ├── REST API (local + outbound events)
                              ├── Audio capture / VAD / upload / playback
                              ├── Display (LVGL)
                              ├── Sensors + alarms
                              └── USB serial CLI
```

Outbound events include `/speech`, `/speech/finalize`, `/ui-event`, `/device/heartbeat`, and sensor/IR events. Inbound commands require matching `target_device_uid`.

## Project layout

Firmware lives under `firmware/` (planned structure):

```
firmware/
  main/
  components/
    app_core/      bootstrap, device identity
    config/        models, validator, NVS store, manager
    network/       Wi-Fi, SNTP, auth, retry
    api/           HTTP server and route handlers
    audio/         capture, VAD, upload, playback, utterance FSM
    display/       screen model/parser, LVGL renderer
    sensors/       battery, environment, radar, alarms
    ir/            learn/send
    cli/           serial CLI
    diagnostics/   health, metrics, log buffer
```

See [`02-project-layout.md`](02-project-layout.md) for the full file tree.

## API

All routes are prefixed with `/api/v1`. Core endpoints:

- **Control:** `POST /play`, `POST /display`, `GET /status`, `GET /health`
- **Config:** `GET /config`, `POST /config/patch`, `POST /config/save`, `POST /config/revert`, …
- **Sensors:** `GET /battery`, `GET /environment`, `POST /ir/learn/start`, `POST /ir/send`

Full contract: [`05-platform-and-api.md`](05-platform-and-api.md)

## Build prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (ESP32-S3 target)
- ESP-BOX board support / BOX-3 examples as hardware reference

```bash
# Once firmware/ is scaffolded:
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Adjust the serial port for your system.

## Implementation order

1. Platform skeleton, config, Wi-Fi, time sync, identity
2. Local REST server, USB CLI
3. Audio path (capture, VAD, upload, playback)
4. Display, sensors, IR, diagnostics

Details: [`08-build-plan.md`](08-build-plan.md)

## Testing

Layered strategy from day one:

- **Host unit tests** — config validator, alarm evaluator, screen parser, retry policy
- **Unity target tests** — NVS store, log buffer, health service
- **Pytest integration** — boot, config flow, REST, Wi-Fi, CLI on a real board
- **Manual smoke** — audio, display, touch, radar, IR

See [`testing_readme.md`](testing_readme.md) and [`manual_smoke_checklist.md`](manual_smoke_checklist.md).

## Documentation

Read in this order:

1. [`00-cursor-readme.md`](00-cursor-readme.md) — mission and constraints
2. [`01-coding-style.md`](01-coding-style.md) — C++ style and docstrings
3. [`02-project-layout.md`](02-project-layout.md) — required file structure
4. [`03-implementation-rules.md`](03-implementation-rules.md) — product decisions
5. [`05-platform-and-api.md`](05-platform-and-api.md) — REST contract
6. [`06-config-and-cli.md`](06-config-and-cli.md) — config model and CLI
7. [`07-display-ir-sensors.md`](07-display-ir-sensors.md) — display, IR, sensors
8. [`08-build-plan.md`](08-build-plan.md) — implementation sequence

Index: [`04-documentation-index.md`](04-documentation-index.md)

## Constraints

- ESP-IDF and plain C++ only (no Arduino)
- One class per file; small, modular components
- No OTA
- Config edits stay in RAM until explicit save
- Auth token required for admin/control endpoints; secrets never logged

## Status

Specification and documentation are in place. Firmware implementation has not started yet.
