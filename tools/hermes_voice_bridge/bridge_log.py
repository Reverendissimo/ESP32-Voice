"""Bridge logging with optional --debug file (unix millisecond timestamps)."""

from __future__ import annotations

import logging
import sys
from pathlib import Path

_LOGGER = logging.getLogger("hermes_voice_bridge")
_CONFIGURED = False


class UnixMsFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        unix_ms = int(record.created * 1000)
        return f"{unix_ms} {record.getMessage()}"


def configure_bridge_log(debug_file: str | Path | None) -> Path | None:
    """Enable timestamped logging to stdout and debug_file. Returns resolved path."""
    global _CONFIGURED

    _LOGGER.handlers.clear()
    _LOGGER.propagate = False

    if debug_file is None:
        _CONFIGURED = False
        _LOGGER.setLevel(logging.WARNING)
        return None

    path = Path(debug_file).expanduser()
    if not path.is_absolute():
        path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)

    formatter = UnixMsFormatter()
    stream_handler = logging.StreamHandler(sys.stdout)
    stream_handler.setFormatter(formatter)
    file_handler = logging.FileHandler(path, encoding="utf-8")
    file_handler.setFormatter(formatter)

    _LOGGER.setLevel(logging.INFO)
    _LOGGER.addHandler(stream_handler)
    _LOGGER.addHandler(file_handler)
    _CONFIGURED = True
    _LOGGER.info(f"[log] debug logging -> {path}")
    return path


def debug_enabled() -> bool:
    return _CONFIGURED


def blog(msg: str) -> None:
    """Pipeline log line (timestamped when --debug is set)."""
    if _CONFIGURED:
        _LOGGER.info(msg)
    else:
        print(msg, flush=True)


def blog_http(client: str, message: str) -> None:
    """HTTP access log (timestamped when --debug is set)."""
    line = f"{client} - {message}"
    if _CONFIGURED:
        _LOGGER.info(f"[http] {line}")
    else:
        sys.stderr.write(f"{line}\n")
