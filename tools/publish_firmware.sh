#!/usr/bin/env bash
# Pack dist/esp32-voice-flash/ into tar.gz (bins + flash.sh).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"
BUILD_BIN="$ROOT/firmware/build/esp32-voice.bin"
OUT_DIR="$ROOT/dist/esp32-voice-flash"
ARCHIVE="$ROOT/dist/esp32-voice-flash.tar.gz"

VERSION="$(grep -E '^project\(esp32-voice VERSION' "$ROOT/firmware/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+)\).*/\1/')"

mkdir -p "$OUT_DIR"

# Prefer fresh build output when present; otherwise keep existing dist bins.
if [[ -f "$BUILD_BIN" ]]; then
  cp -f "$BUILD_BIN" "$OUT_DIR/esp32-voice.bin"
  [[ -f "$ROOT/firmware/build/bootloader/bootloader.bin" ]] && \
    cp -f "$ROOT/firmware/build/bootloader/bootloader.bin" "$OUT_DIR/bootloader.bin"
  [[ -f "$ROOT/firmware/build/partition_table/partition-table.bin" ]] && \
    cp -f "$ROOT/firmware/build/partition_table/partition-table.bin" "$OUT_DIR/partition-table.bin"
  [[ -f "$ROOT/firmware/build/ota_data_initial.bin" ]] && \
    cp -f "$ROOT/firmware/build/ota_data_initial.bin" "$OUT_DIR/ota_data_initial.bin"
elif [[ ! -f "$OUT_DIR/esp32-voice.bin" ]]; then
  echo "No firmware binary — place esp32-voice.bin in $OUT_DIR or run idf.py build" >&2
  exit 1
fi

cp -f "$DIR/flash_from_host.sh" "$OUT_DIR/flash.sh"
cp -f "$DIR/flash_from_host.sh" "$OUT_DIR/flash_from_host.sh"
chmod +x "$OUT_DIR/flash.sh" "$OUT_DIR/flash_from_host.sh"

printf 'esp32-voice %s\n' "$VERSION" >"$OUT_DIR/VERSION.txt"

rm -f "$ARCHIVE"
tar -czf "$ARCHIVE" -C "$ROOT/dist" esp32-voice-flash

echo "Created $ARCHIVE ($(stat -c%s "$ARCHIVE") bytes, firmware $VERSION)"
