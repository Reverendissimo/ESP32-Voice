#!/usr/bin/env bash
# picocom wrapper for BOX-3 USB Serial/JTAG (avoid DTR reset on connect).
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"

if [[ ! -e "$PORT" ]]; then
  echo "Port not found: $PORT" >&2
  exit 1
fi

sleep 2
# --noinit: do not change serial port settings (incl. DTR) on startup — picocom 3.x
# Fallback: plain picocom if flag unsupported
if picocom --help 2>&1 | grep -q noreset; then
  exec picocom "$PORT" -b 115200 --noreset
elif picocom --help 2>&1 | grep -q noinit; then
  exec picocom "$PORT" -b 115200 --noinit
else
  echo "Tip: if picocom exits instantly, use: ./tools/monitor.sh $PORT" >&2
  exec picocom "$PORT" -b 115200
fi
