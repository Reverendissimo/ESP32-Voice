# Manual Smoke Checklist

## Before running
- firmware builds successfully
- board flashes successfully
- serial console opens successfully
- local network available

## Smoke checks

### Boot and identity
- device boots cleanly
- `device_uid` is shown in logs or health output
- firmware version is visible

### CLI
- `help` works
- `status` works
- `config_show` works
- `health` works

### REST
- `/health` returns valid JSON
- `/metrics` returns valid JSON
- `/api/version` returns valid JSON
- `/config` and `/config/saved` behave as expected

### Config persistence
- patch RAM config
- verify active changes only
- save config
- reboot
- verify persistence
- patch again and revert
- verify saved values restored

### Wi-Fi safety
- test Wi-Fi change without immediate save
- confirm failure path does not brick access

### Audio and display
- trigger speech upload flow
- send async `/play`
- verify audio plays
- send async `/display`
- verify screen updates

### Sensors
- battery endpoint responds
- environment endpoint responds
- alarm thresholds present in config

### IR
- start learn mode
- verify timeout path
- if remote is available, verify capture path

### Time and diagnostics
- verify time starts untrusted before sync if expected
- verify trusted after successful sync
- verify logs endpoint returns recent entries
