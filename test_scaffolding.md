# Test Scaffolding Instructions

## Host tests

Use ESP-IDF Linux host testing for logic that can run without target hardware.[cite:566][cite:573]

Practical guidance:
- isolate pure logic from ESP-specific code
- inject dependencies via interfaces
- mock NVS, time, network, and hardware facades where needed
- keep host tests fast

Good candidates for host testing:
- config merge logic
- validators
- alarm evaluator
- JSON schema validation/parsing
- retry policy
- response builders

## Target Unity tests

Use component `test/` directories with `test_*.c` or appropriate supported test files so Unity discovery works as expected in ESP-IDF tooling.[cite:564][cite:576][cite:567]

Rules:
- keep target tests focused
- do not depend on unrelated peripherals if avoidable
- test component contracts, not entire product flows

## Pytest hardware tests

Use pytest-based automation for board-level integration tests and serial/log-based assertions.[cite:565][cite:574][cite:568]

Practical rules:
- one board is enough for initial automation
- keep tests idempotent
- prefer state verification via REST and serial output
- isolate tests that require real sensor interaction
- mark hardware-fragile tests clearly

## Recommended fixture capabilities

Create reusable pytest fixtures for:
- flashing firmware
- waiting for boot
- reading serial output
- calling local REST endpoints
- forcing reboot
- capturing device identity
- reading health/status
- opening CLI session

## Manual smoke checklist

Keep a short manual checklist for:
- audio playback audibility
- display visual correctness
- touch interaction
- radar presence behavior
- IR learn with a real remote
