# Test Strategy

## Goal

Build a practical test suite for an ESP-IDF firmware project with a mix of:
- host-based unit tests
- target-based component tests
- hardware integration tests
- API contract tests
- smoke tests for manual bring-up

ESP-IDF supports target-based unit tests with Unity, host-side automation with pytest, and Linux host testing for logic isolated from hardware dependencies.[cite:564][cite:565][cite:566]

## Test layers

### 1. Host unit tests

Use Linux host tests for logic-heavy components that do not need real hardware and can be isolated behind mocks or interfaces.[cite:566][cite:573]

Target these components first:
- config validator
- config patch merge logic
- alarm evaluator
- retry policy
- screen parser
- request/response model validation
- device identity formatting helpers
- error response factory

### 2. Target component tests

Use ESP-IDF Unity tests for component-level target tests that need the ESP environment or IDF facilities.[cite:564][cite:567][cite:576]

Target these components:
- config store with NVS test partition strategy
- time sync state object
- HTTP route validation helpers
- log ring buffer
- battery state translation

### 3. Hardware integration tests

Use pytest-based target orchestration for board-level tests where real hardware behavior matters.[cite:565][cite:574][cite:568]

Target these flows:
- boot and identity
- Wi-Fi connect/disconnect
- config save/load/revert
- local REST endpoints
- async playback endpoint behavior
- display endpoint behavior
- sensor endpoint availability
- IR learn timeout path
- CLI basic command set

### 4. Manual smoke tests

Keep a short manual checklist for peripherals that are painful to fully automate early.

## Required repository test layout

```text
firmware/
  components/
    config/
      test/
      host_test/
    network/
      test/
      host_test/
    api/
      test/
      host_test/
    audio/
      test/
      host_test/
    display/
      test/
      host_test/
    sensors/
      test/
      host_test/
    ir/
      test/
      host_test/
    diagnostics/
      test/
      host_test/

  tests/
    pytest/
      conftest.py
      test_boot_identity.py
      test_config_flow.py
      test_rest_health.py
      test_rest_play_display.py
      test_wifi_flow.py
      test_cli_smoke.py
      test_sensor_endpoints.py
      test_ir_learning_timeout.py

  docs/
    manual_smoke_checklist.md
```

## Coverage objective

Minimum target:
- critical pure logic: high coverage
- config and alarm behavior: very high confidence
- hardware-bound behavior: scenario coverage, not synthetic percentage chasing

Do not optimize for vanity percentages. Optimize for confidence in the risky flows.
