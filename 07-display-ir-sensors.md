# Display, IR, Sensors, and Alarms Directive

## Display

Implement display as a constrained declarative JSON schema rendered locally.

### Rules
- no HTML
- no browser model
- no arbitrary widget trees in v1
- use fixed supported component kinds
- use full-screen replacement in v1

### Supported display modes
- idle
- listening
- thinking
- message
- choice_list
- confirm
- notification
- form

### Supported components
- text
- badge
- button
- list
- input
- progress

## IR

Implement:
- `POST /ir/learn/start`
- `POST /ir/send`
- outbound `POST /ir/event`

### IR learning behavior
- modal state
- timeout
- on-screen wizard
- report success, timeout, cancel, error
- preserve decoded and raw data when possible

## Sensors

Implement these sensor-facing services:
- battery service
- environment service for temperature/humidity
- radar presence service

## Battery

Expose:
- voltage
- percent
- charging if detectable
- external power if detectable
- low / critical state

Send events when crossing configured thresholds.

## Environment

Expose:
- temperature
- humidity

Send events on threshold crossing or meaningful deltas if configured.

## Alarms

Alarm config must include:
- enabled flag
- trigger threshold
- clear threshold or hysteresis
- min duration if needed
- cooldown if needed

Avoid repeated flapping around thresholds.
