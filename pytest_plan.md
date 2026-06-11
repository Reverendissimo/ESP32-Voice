# Pytest Plan

## Test files to create

```text
tests/pytest/
  conftest.py
  test_boot_identity.py
  test_config_flow.py
  test_revert_flow.py
  test_wifi_flow.py
  test_rest_health.py
  test_rest_target_validation.py
  test_play_endpoint.py
  test_display_endpoint.py
  test_sensor_endpoints.py
  test_time_sync.py
  test_cli_smoke.py
  test_ir_timeout.py
```

## Per-file intent

- `test_boot_identity.py`: boot, version, identity availability
- `test_config_flow.py`: patch/apply/save/load lifecycle
- `test_revert_flow.py`: unsaved changes rollback
- `test_wifi_flow.py`: safe Wi-Fi test path
- `test_rest_health.py`: health/metrics/version/time endpoints
- `test_rest_target_validation.py`: reject wrong `target_device_uid`
- `test_play_endpoint.py`: playback command validation
- `test_display_endpoint.py`: display schema validation
- `test_sensor_endpoints.py`: battery/environment endpoint behavior
- `test_time_sync.py`: trusted/untrusted transitions
- `test_cli_smoke.py`: serial CLI core commands
- `test_ir_timeout.py`: IR learn timeout path
```
