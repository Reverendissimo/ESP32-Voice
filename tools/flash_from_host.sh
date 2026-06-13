#!/usr/bin/env bash
# Flash ESP32-S3-BOX-3 from a host with USB access (not LXC).
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
BAUD="${2:-460800}"
ERASE="${3:-}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# When run from dist/esp32-voice-flash/, bins are alongside this script.
if [[ -f "$DIR/esp32-voice.bin" ]]; then
  BUNDLE_DIR="$DIR"
elif [[ -f "$DIR/../dist/esp32-voice-flash/esp32-voice.bin" ]]; then
  BUNDLE_DIR="$(cd "$DIR/../dist/esp32-voice-flash" && pwd)"
else
  BUNDLE_DIR="$DIR"
fi

if [[ ! -e "$PORT" ]]; then
  echo "Serial port not found: $PORT" >&2
  echo "Try: ls /dev/ttyACM* /dev/serial/by-id/" >&2
  exit 1
fi

if ! python3 -m esptool version >/dev/null 2>&1; then
  echo "Install esptool: pip install esptool" >&2
  exit 1
fi

ESPTOOL=(python3 -m esptool)

# ESP-IDF always flashes with dio for BOX-3/QIO runtime builds (see firmware/build/flash_args).
FLASH_MODE=dio

if [[ "$ERASE" == "--erase" || "$ERASE" == "erase" ]]; then
  echo "Erasing entire flash on $PORT ..."
  "${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" -b "$BAUD" erase-flash
fi

echo "Flashing to $PORT (mode=$FLASH_MODE) ..."
"${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" -b "$BAUD" \
  --before default-reset --after no-reset write-flash \
  --flash-mode "$FLASH_MODE" --flash-freq 80m --flash-size 16MB \
  0x0 "$BUNDLE_DIR/bootloader.bin" \
  0x10000 "$BUNDLE_DIR/partition-table.bin" \
  0x20000 "$BUNDLE_DIR/esp32-voice.bin"

echo "Done. Press RESET on the BOX-3, wait 3s, then monitor WITHOUT toggling DTR/RTS:"
echo "  python3 -m serial.tools.miniterm $PORT 115200 --dtr 0 --rts 0"
