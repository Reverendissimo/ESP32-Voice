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
if [[ "${BRIDGE_STREAMING_ASR:-0}" == "1" ]]; then
  BRIDGE_ARGS+=(--streaming-asr)
fi
if [[ -n "${BRIDGE_TTS_ATTN:-}" ]]; then
  BRIDGE_ARGS+=(--tts-attn "$BRIDGE_TTS_ATTN")
fi
if [[ -n "${TTS_VOICE_WAV:-}" ]]; then
  BRIDGE_ARGS+=(--tts-voice-wav "$TTS_VOICE_WAV")
fi
if [[ -n "${TTS_IDLE_FLUSH_MS:-}" ]]; then
  BRIDGE_ARGS+=(--tts-idle-flush-ms "$TTS_IDLE_FLUSH_MS")
fi
if [[ -n "${TTS_PLAY_LEAD_MS:-}" ]]; then
  BRIDGE_ARGS+=(--tts-play-lead-ms "$TTS_PLAY_LEAD_MS")
fi
if [[ -n "${TTS_COALESCE_MS:-}" ]]; then
  BRIDGE_ARGS+=(--tts-coalesce-ms "$TTS_COALESCE_MS")
fi
if [[ -n "${TTS_IDLE_FLUSH_MIN_CHARS:-}" ]]; then
  BRIDGE_ARGS+=(--tts-idle-flush-min-chars "$TTS_IDLE_FLUSH_MIN_CHARS")
fi
if [[ -n "${PLAY_CHUNK_BYTES:-}" ]]; then
  BRIDGE_ARGS+=(--play-chunk-bytes "$PLAY_CHUNK_BYTES")
fi
if [[ -n "${VOICE_TOOLS_OPENAI_KEY:-}" ]]; then
  BRIDGE_ARGS+=(--voice-tools-openai-key "$VOICE_TOOLS_OPENAI_KEY")
fi
if [[ -n "${OPENAI_STT_MODEL:-}" ]]; then
  BRIDGE_ARGS+=(--openai-stt-model "$OPENAI_STT_MODEL")
fi
if [[ -n "${OPENAI_TTS_MODEL:-}" ]]; then
  BRIDGE_ARGS+=(--openai-tts-model "$OPENAI_TTS_MODEL")
fi
if [[ -n "${BRIDGE_DEBUG:-}" ]]; then
  if [[ "${BRIDGE_DEBUG}" == "1" ]]; then
    BRIDGE_ARGS+=(--debug)
  else
    BRIDGE_ARGS+=(--debug "$BRIDGE_DEBUG")
  fi
fi

if [[ -x "$ROOT/.venv/bin/python" ]]; then
  PYTHON="$ROOT/.venv/bin/python"
else
  PYTHON=python3
fi

exec "$PYTHON" "$DIR/server.py" "${HERMES_ARGS[@]}" "${BRIDGE_ARGS[@]}" "$@"
