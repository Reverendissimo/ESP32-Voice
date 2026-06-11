# Config and CLI Directive

## Config architecture

Implement three layers:

- defaults
- saved config in NVS
- active config in RAM

The code must make these layers visually obvious.

## Required config sections

Implement config sections for:
- identity
- auth
- wifi
- network
- callbacks
- presence
- vad
- audio
- display
- ir
- battery
- environment
- alarms
- time
- diagnostics

## Important config requirements

- Wi-Fi SSID and password are part of config.
- Passwords/tokens must be masked in normal read responses.
- Alarm thresholds must be in config.
- Clear thresholds or hysteresis must be in config.
- Sensor behavior must not use hardcoded thresholds hidden in code.

## Required config workflow

- patch active RAM config
- validate
- optionally apply live
- optionally test Wi-Fi
- explicitly save to NVS
- allow revert/load from saved

## CLI requirements

Provide a minimal plain serial terminal CLI.

It must work in a normal serial terminal app.

### Required commands

- `help`
- `status`
- `wifi_set <ssid> <password>`
- `wifi_test`
- `config_show`
- `config_show_saved`
- `config_load`
- `config_save`
- `config_revert`
- `reboot`
- `factory_reset`
- `time_sync`
- `health`

## CLI behavior rules

- Use human-readable text output.
- Do not require JSON input.
- Do not echo secrets.
- Confirm dangerous commands.
- Keep parsing simple and robust.
