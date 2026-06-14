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

if [[ -x "$ROOT/.venv/bin/python" ]]; then
  PYTHON="$ROOT/.venv/bin/python"
else
  PYTHON=python3
fi

exec "$PYTHON" "$DIR/server.py" "${HERMES_ARGS[@]}" "$@"
