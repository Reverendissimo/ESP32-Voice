#!/usr/bin/env bash
# Monitor ESP32-S3-BOX-3 USB Serial/JTAG without toggling DTR/reset.
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"

echo "Waiting for $PORT ..."
for i in $(seq 1 30); do
  if [[ -e "$PORT" ]]; then
    break
  fi
  sleep 0.5
done

if [[ ! -e "$PORT" ]]; then
  echo "Port not found: $PORT" >&2
  echo "Check: ls /dev/ttyACM* /dev/serial/by-id/  &&  dmesg | tail -20" >&2
  exit 1
fi

# Let USB settle after enumerate/reboot
sleep 2

echo "Opening monitor on $PORT (DTR/RTS off — no auto-reset)"
echo "Exit miniterm: Ctrl+] then type quit"
echo "---"

exec python3 -m serial.tools.miniterm "$PORT" 115200 --raw --eol LF --filter direct --dtr 0 --rts 0
