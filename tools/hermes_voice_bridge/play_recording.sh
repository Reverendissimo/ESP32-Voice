#!/usr/bin/env bash
# Play a saved utterance WAV (default: most recent in recordings/).
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
REC="${DIR}/recordings"
if [[ $# -ge 1 ]]; then
  WAV="$1"
else
  WAV="$(ls -t "${REC}"/*.wav 2>/dev/null | head -1)"
fi
if [[ -z "${WAV}" || ! -f "${WAV}" ]]; then
  echo "No recording found in ${REC}" >&2
  exit 1
fi
python3 - <<PY
import wave
p = "${WAV}"
with wave.open(p, "rb") as w:
    n, r, c = w.getnframes(), w.getframerate(), w.getnchannels()
print(f"Playing {p} ({n/r:.2f}s, {r} Hz, {c} ch)")
PY
aplay -q "${WAV}"
