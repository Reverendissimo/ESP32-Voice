# Test Matrix

## Highest-risk areas

These are the most important areas to test first:

1. Config persistence and revert behavior
2. Wi-Fi credential updates without bricking the device
3. Device identity correctness and target validation
4. Alarm threshold behavior with clear thresholds / hysteresis
5. Async playback and display command routing
6. USB CLI recovery path
7. Time sync trusted/untrusted state

## Required unit test matrix

| Component | Test cases |
|---|---|
| `ConfigValidator` | required fields, invalid ranges, malformed patches, masked secret fields |
| `ConfigManager` | patch active config, apply, save, load, revert, reset |
| `AlarmEvaluator` | high trigger, high clear, low trigger, low clear, duration gate, cooldown, no flapping |
| `RetryPolicy` | backoff sequence, max cap, reset after success |
| `ScreenParser` | valid screens, unsupported component rejection, missing required field rejection |
| `ErrorResponseFactory` | stable error schema, request ID propagation |
| `DeviceIdentity` | consistent UID format, non-empty identity, target match logic |
| `AuthContext` | token required, invalid token rejection, masked output |

## Required target test matrix

| Component | Test cases |
|---|---|
| NVS config store | save, load, overwrite, reset defaults |
| Health service | uptime, dirty flag, time trusted flag present |
| Log buffer | append, cap at limit, retrieve recent entries |
| Battery mapping | threshold state transitions |
| Route validation | wrong target device rejected |

## Required integration test matrix

| Flow | Test cases |
|---|---|
| Boot | device boots, exposes version, health returns identity |
| Config | patch RAM config, save, reboot, load persisted config |
| Revert | patch RAM config, revert, verify saved state remains unchanged |
| Wi-Fi | test credentials before save, invalid credentials rejected safely |
| REST health | health, metrics, time, version endpoints respond |
| REST targeting | inbound command with wrong target device rejected |
| Playback | valid `/play` accepted, bad payload rejected |
| Display | valid `/display` accepted, bad schema rejected |
| Sensors | battery/environment endpoints respond even if values unavailable with explicit status |
| CLI | help/status/config_save/wifi_test work through serial |
| Time | sync state changes from untrusted to trusted when network time available |
| IR | learn start enters modal state, timeout event generated |
