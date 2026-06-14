#!/usr/bin/env bash
# Start Hermes-Voice-Bridge (ESP32 speech → ASR → Hermes → TTS → playback).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

if [[ -f "$DIR/.env.local" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "$DIR/.env.local"
  set +a
fi

HERMES_ARGS=()
if [[ -n "${HERMES_HOST:-}" ]]; then
  HERMES_ARGS+=(--hermes-host "$HERMES_HOST")
fi
if [[ -n "${HERMES_API_KEY:-}" ]]; then
  HERMES_ARGS+=(--hermes-api-key "$HERMES_API_KEY")
fi

BRIDGE_ARGS=()
if [[ -n "${ESP_DEVICE_IP:-}" ]]; then
  BRIDGE_ARGS+=(--device-ip "$ESP_DEVICE_IP")
elif [[ -n "${ECHO_DEVICE_IP:-}" ]]; then
  BRIDGE_ARGS+=(--echo-device-ip "$ECHO_DEVICE_IP")
fi
if [[ -n "${OTA_SECRET:-}" ]]; then
  BRIDGE_ARGS+=(--ota-secret "$OTA_SECRET")
fi
if [[ -n "${ESP_AUTH_TOKEN:-}" ]]; then
  BRIDGE_ARGS+=(--echo-auth-token "$ESP_AUTH_TOKEN")
elif [[ -n "${ECHO_AUTH_TOKEN:-}" ]]; then
  BRIDGE_ARGS+=(--echo-auth-token "$ECHO_AUTH_TOKEN")
fi

if [[ -x "$ROOT/.venv/bin/python" ]]; then
  PYTHON="$ROOT/.venv/bin/python"
else
  PYTHON=python3
fi

exec "$PYTHON" "$DIR/server.py" "${HERMES_ARGS[@]}" "${BRIDGE_ARGS[@]}" "$@"
