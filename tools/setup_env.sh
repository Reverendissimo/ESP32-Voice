#!/usr/bin/env bash
# Source ESP-IDF and activate project Python venv.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
  echo "ESP-IDF not found at $IDF_PATH" >&2
  echo "Clone with: git clone --recursive -b v5.4.1 https://github.com/espressif/esp-idf.git $IDF_PATH" >&2
  return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1090
source "$IDF_PATH/export.sh"

if [[ -f "$ROOT/.venv/bin/activate" ]]; then
  # shellcheck disable=SC1091
  source "$ROOT/.venv/bin/activate"
fi

export ESP32_VOICE_ROOT="$ROOT"
export ESP32_VOICE_FIRMWARE="$ROOT/firmware"
