#!/usr/bin/env bash
# Start the ESP32-Voice A2 speech test server.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

if [[ -x "$ROOT/.venv/bin/python" ]]; then
  PYTHON="$ROOT/.venv/bin/python"
else
  PYTHON=python3
fi

exec "$PYTHON" "$DIR/server.py" "$@"
