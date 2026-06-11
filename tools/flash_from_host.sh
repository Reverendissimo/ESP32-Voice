#!/usr/bin/env bash
# Flash ESP32-S3-BOX-3 from a host with USB access (not LXC).
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
BAUD="${2:-460800}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -e "$PORT" ]]; then
  echo "Serial port not found: $PORT" >&2
  echo "Try: ls /dev/ttyACM* /dev/serial/by-id/" >&2
  exit 1
fi

if ! command -v esptool.py >/dev/null 2>&1 && ! python3 -m esptool version >/dev/null 2>&1; then
  echo "Install esptool: pip install esptool" >&2
  exit 1
fi

ESPTOOL=(python3 -m esptool)

echo "Flashing to $PORT ..."
"${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" -b "$BAUD" \
  --before default_reset --after hard_reset write_flash \
  --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 "$DIR/bootloader.bin" \
  0x8000 "$DIR/partition-table.bin" \
  0x10000 "$DIR/esp32-voice.bin"

echo "Done. Serial monitor (pick one):"
echo "  idf.py -p $PORT monitor          # if ESP-IDF installed"
echo "  python3 -m serial.tools.miniterm $PORT 115200"
echo "  picocom $PORT -b 115200"
