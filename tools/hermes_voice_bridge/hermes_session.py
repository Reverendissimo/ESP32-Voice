"""Persist and resolve active Hermes conversation session ids."""

from __future__ import annotations

import json
import re
import secrets
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

DEFAULT_SESSION_FILE = Path(__file__).resolve().parent / ".hermes_session.json"


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def sanitize_session_id(value: str, *, max_len: int = 64) -> str:
    cleaned = re.sub(r"[^a-zA-Z0-9_-]", "-", value.strip()).strip("-")
    if not cleaned:
        raise ValueError("session id is empty after sanitization")
    return cleaned[:max_len]


def generate_session_id(prefix: str = "voice-box") -> str:
    safe_prefix = re.sub(r"[^a-zA-Z0-9_-]", "-", prefix.strip()).strip("-") or "voice-box"
    return f"{safe_prefix}-{secrets.token_hex(8)}"


def load_store(path: Path = DEFAULT_SESSION_FILE) -> dict[str, Any]:
    if not path.is_file():
        return {"active": "", "history": []}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {"active": "", "history": []}
    if not isinstance(data, dict):
        return {"active": "", "history": []}
    data.setdefault("active", "")
    data.setdefault("history", [])
    return data


def save_store(store: dict[str, Any], path: Path = DEFAULT_SESSION_FILE) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(store, indent=2) + "\n", encoding="utf-8")


def _append_history(store: dict[str, Any], session_id: str, *, reason: str) -> None:
    history = store.setdefault("history", [])
    if not isinstance(history, list):
        history = []
        store["history"] = history
    entry = {
        "id": session_id,
        "reason": reason,
        "activated_at": _now_iso(),
    }
    if history and isinstance(history[-1], dict) and history[-1].get("id") == session_id:
        history[-1] = entry
    else:
        history.append(entry)
    store["history"] = history[-50:]


def activate_session(
    session_id: str,
    *,
    path: Path = DEFAULT_SESSION_FILE,
    reason: str,
) -> str:
    cleaned = sanitize_session_id(session_id)
    store = load_store(path)
    store["active"] = cleaned
    _append_history(store, cleaned, reason=reason)
    save_store(store, path)
    return cleaned


def create_new_session(
    *,
    path: Path = DEFAULT_SESSION_FILE,
    prefix: str = "voice-box",
) -> str:
    return activate_session(generate_session_id(prefix), path=path, reason="new")


def resolve_active_session(
    *,
    path: Path = DEFAULT_SESSION_FILE,
    prefix: str = "voice-box",
    new_session: bool = False,
    session_id: str | None = None,
) -> tuple[str, str]:
    """Return (active_session_id, mode) where mode is new|restored|continued|created."""
    if new_session and session_id:
        raise ValueError("use only one of new_session and session_id")

    if new_session:
        return create_new_session(path=path, prefix=prefix), "new"

    if session_id:
        restored = activate_session(session_id, path=path, reason="restored")
        return restored, "restored"

    store = load_store(path)
    active = str(store.get("active", "")).strip()
    if active:
        return sanitize_session_id(active), "continued"

    created = create_new_session(path=path, prefix=prefix)
    return created, "created"
