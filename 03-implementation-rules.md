# Implementation Rules

## Core product decisions

Implement these exact decisions unless a technical blocker forces a revision:

- Wake model: **B**
- Audio protocol: **A2**
- Display complexity: **D1**
- No OTA
- Config edits go to RAM first
- Save to NVS only on explicit save
- Wi-Fi credentials included in config
- Minimal USB serial CLI required
- Unique hardware-derived device ID required in all REST traffic

## Audio rules

- Start upload when VAD detects speech.
- Keep streaming through short pauses.
- Finalize conservatively after a longer silence timeout.
- Do not make the ESP too eager to declare sentence end.
- Playback is async via dedicated endpoint, not tied to speech upload response.

## Config rules

- Keep three layers: defaults, saved, active.
- `PATCH` affects active RAM config only.
- `SAVE` persists active config to NVS.
- `LOAD` reloads saved config into active RAM config.
- `REVERT` discards unsaved changes.
- Wi-Fi changes must support a test-before-save workflow.

## Sensor/alarm rules

- Temperature, humidity, battery, and alarm thresholds must be configurable.
- Alarm config must include trigger and clear thresholds or equivalent hysteresis.
- Avoid alarm flapping.
- Use explicit durations and cooldown where useful.

## IR rules

- Learning mode must be modal.
- Learning mode must have timeout.
- Learned code should preserve both decoded and raw forms when possible.
- Server-triggered send must validate target device identity.

## Display rules

- Use fixed JSON schema.
- No HTML.
- No arbitrary widget nesting in first version.
- Use full-screen replacement, not a complex patch engine in v1.

## Device identity rules

- Derive immutable `device_uid` from hardware identity.
- Keep user-defined `device_name` editable.
- Include target/source identity in all REST interactions.
- Reject inbound commands meant for another device.

## Security rules

- Require auth token for admin and control endpoints.
- Never log secrets.
- Do not expose plain passwords in config reads.

## Diagnostics rules

- Expose health, metrics, recent logs, API version, firmware version, time sync state.
- Add request IDs / command IDs / session IDs where relevant.
- Make failure reasons inspectable.
