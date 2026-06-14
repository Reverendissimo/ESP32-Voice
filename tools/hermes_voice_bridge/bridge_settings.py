"""Persist bridge UI connection settings (device IP, OTA secret, etc.)."""

from __future__ import annotations

import json
import os
import threading
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


@dataclass
class BridgeSettings:
    device_ip: str = ""
    ota_secret: str = ""
    auth_token: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> BridgeSettings:
        return cls(
            device_ip=str(data.get("device_ip", "") or ""),
            ota_secret=str(data.get("ota_secret", "") or ""),
            auth_token=str(data.get("auth_token", "") or ""),
        )


class BridgeSettingsStore:
    def __init__(self, path: Path) -> None:
        self._path = path
        self._lock = threading.Lock()
        self._settings = BridgeSettings()
        self.load()

    @property
    def settings(self) -> BridgeSettings:
        with self._lock:
            return BridgeSettings.from_dict(self._settings.to_dict())

    def update(self, **kwargs: Any) -> BridgeSettings:
        with self._lock:
            data = self._settings.to_dict()
            for key, value in kwargs.items():
                if key in data and value is not None:
                    data[key] = str(value)
            self._settings = BridgeSettings.from_dict(data)
            self._save_unlocked()
            return BridgeSettings.from_dict(data)

    def load(self) -> BridgeSettings:
        with self._lock:
            if self._path.is_file():
                try:
                    raw = json.loads(self._path.read_text(encoding="utf-8"))
                    if isinstance(raw, dict):
                        self._settings = BridgeSettings.from_dict(raw)
                except (OSError, json.JSONDecodeError):
                    pass
            return BridgeSettings.from_dict(self._settings.to_dict())

    def apply_defaults(self, **kwargs: Any) -> BridgeSettings:
        """Fill empty fields only (does not overwrite user-saved values)."""
        with self._lock:
            data = self._settings.to_dict()
            changed = False
            for key, value in kwargs.items():
                if key not in data or value is None:
                    continue
                text = str(value).strip()
                if not text:
                    continue
                if not str(data.get(key, "") or "").strip():
                    data[key] = text
                    changed = True
            if changed:
                self._settings = BridgeSettings.from_dict(data)
                self._save_unlocked()
            return BridgeSettings.from_dict(data)


    def _save_unlocked(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._path.write_text(json.dumps(self._settings.to_dict(), indent=2) + "\n", encoding="utf-8")


def settings_defaults_from_env() -> dict[str, str]:
    return {
        "device_ip": (
            os.environ.get("ESP_DEVICE_IP", "")
            or os.environ.get("DEVICE_IP", "")
            or os.environ.get("ECHO_DEVICE_IP", "")
        ).strip(),
        "ota_secret": (
            os.environ.get("OTA_SECRET", "")
            or os.environ.get("ESP_OTA_SECRET", "")
        ).strip(),
        "auth_token": (
            os.environ.get("ESP_AUTH_TOKEN", "")
            or os.environ.get("ECHO_AUTH_TOKEN", "")
        ).strip(),
    }
