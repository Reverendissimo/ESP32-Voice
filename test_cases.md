# Concrete Test Cases

## Host unit tests

### ConfigManager
- patch valid RAM config -> success
- patch invalid RAM config -> reject and preserve previous active values
- save active config -> persistence request issued
- load saved config -> active config replaced
- revert active config -> unsaved changes discarded
- reset active config -> defaults restored in RAM only

### AlarmEvaluator
- high threshold crossing activates pending state
- pending alarm does not activate before min duration
- alarm activates after min duration
- alarm clears only at configured clear threshold
- repeated oscillation near threshold does not flap state
- cooldown suppresses duplicate notifications

### ScreenParser
- accepts valid message screen
- rejects unsupported component type
- rejects malformed list item shape
- rejects missing required root fields
- ignores optional unknown cosmetic fields only if policy allows

### RetryPolicy
- exponential growth is correct
- backoff cap is respected
- reset after success returns to base delay

### DeviceIdentity
- hardware identifier conversion produces stable UID string
- wrong target comparison fails
- exact target comparison passes

## Target component tests

### ConfigStore with NVS
- save config blob
- load config blob
- save newer config blob overwrites previous value
- reset saved config restores defaults

### Log buffer
- push entries
- old entries discarded at capacity limit
- retrieval order is correct

### Health service
- includes `device_uid`
- includes firmware version
- includes config dirty flag
- includes time trusted flag

## Integration tests

### Config flow
1. boot board
2. call `GET /config`
3. patch active RAM config
4. verify `GET /config` changed
5. verify `GET /config/saved` unchanged
6. call `POST /config/save`
7. reboot board
8. verify saved values restored

### Revert flow
1. patch active RAM config
2. verify active changed
3. call `POST /config/revert`
4. verify active equals saved

### Wrong-device protection
1. send `/play` with incorrect `target_device_uid`
2. expect structured error and no playback action
3. send `/display` with incorrect target
4. expect structured error and no display update

### Wi-Fi safety
1. patch Wi-Fi credentials in RAM
2. call `/wifi/test`
3. on failure verify saved Wi-Fi unchanged
4. verify CLI recovery still available

### Time sync
1. boot without network or before sync
2. verify `time_trusted=false`
3. enable network / trigger sync
4. verify `time_trusted=true`

### CLI smoke
1. open serial terminal
2. run `help`
3. run `status`
4. run `config_show`
5. run `wifi_test`
6. run `health`
