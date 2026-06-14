"""Persist paired ESP32 devices for multi-device bridge admin."""

from __future__ import annotations

import json
import secrets
import threading
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from bridge_settings import BridgeSettings


def _now() -> float:
    return time.time()


def _normalize_uid(device_uid: str, ip: str) -> str:
    uid = str(device_uid or "").strip()
    if uid:
        return uid
    ip = str(ip or "").strip()
    if ip:
        return f"unknown-{ip.replace('.', '-')}"
    return ""


@dataclass
class PairedDevice:
    device_uid: str
    device_name: str = ""
    ip: str = ""
    ota_secret: str = ""
    auth_token: str = ""
    wifi_password: str = ""
    firmware_version: str = ""
    last_seen_at: float = 0.0
    paired_at: float = 0.0
    source: str = "auto"

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> PairedDevice:
        return cls(
            device_uid=str(data.get("device_uid", "") or ""),
            device_name=str(data.get("device_name", "") or ""),
            ip=str(data.get("ip", "") or ""),
            ota_secret=str(data.get("ota_secret", "") or ""),
            auth_token=str(data.get("auth_token", "") or ""),
            wifi_password=str(data.get("wifi_password", "") or ""),
            firmware_version=str(data.get("firmware_version", "") or ""),
            last_seen_at=float(data.get("last_seen_at", 0.0) or 0.0),
            paired_at=float(data.get("paired_at", 0.0) or 0.0),
            source=str(data.get("source", "auto") or "auto"),
        )


@dataclass
class DeviceRegistryData:
    schema_version: int = 1
    active_device_uid: str = ""
    global_ota_secret: str = ""
    global_auth_token: str = ""
    devices: dict[str, PairedDevice] = field(default_factory=dict)


def fetch_device_uid(ip: str, auth_token: str = "", timeout: float = 5.0) -> tuple[str, str, str]:
    """Return (device_uid, device_name, firmware_version) from GET /api/v1/version."""
    ip = str(ip or "").strip()
    if not ip:
        return "", "", ""
    headers = {"Accept": "application/json"}
    if auth_token:
        headers["X-Auth-Token"] = auth_token
    req = Request(f"http://{ip}/api/v1/version", headers=headers, method="GET")
    try:
        with urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8")
            payload = json.loads(raw) if raw else {}
    except (HTTPError, URLError, json.JSONDecodeError, OSError):
        return "", "", ""
    if not isinstance(payload, dict):
        return "", "", ""
    return (
        str(payload.get("device_uid", "") or ""),
        "",
        str(payload.get("firmware_version", "") or ""),
    )


def generate_auth_token(device_uid: str = "") -> str:
    slug = ""
    uid = str(device_uid or "").strip()
    if uid:
        slug = uid.replace("espbox-", "").replace("unknown-", "")[:10]
    if not slug:
        slug = secrets.token_hex(4)
    return f"ev-{slug}-{secrets.token_hex(12)}"[:63]


class DeviceRegistry:
    def __init__(self, path: Path) -> None:
        self._path = path
        self._lock = threading.Lock()
        self._data = DeviceRegistryData()
        self.load()

    def load(self) -> DeviceRegistryData:
        with self._lock:
            if self._path.is_file():
                try:
                    raw = json.loads(self._path.read_text(encoding="utf-8"))
                    if isinstance(raw, dict):
                        self._data = self._from_file_dict(raw)
                except (OSError, json.JSONDecodeError):
                    pass
            return self._copy_data()

    def ensure_auth_token(self, device_uid: str) -> str:
        uid = str(device_uid or "").strip()
        if not uid:
            return ""
        with self._lock:
            device = self._data.devices.get(uid)
            if device and device.auth_token.strip():
                return device.auth_token.strip()
        token = generate_auth_token(uid)
        self.upsert_device(uid, auth_token=token)
        return token

    def ensure_all_auth_tokens(self) -> None:
        for device in self.list_devices():
            self.ensure_auth_token(device.device_uid)

    def apply_global_defaults(self, **kwargs: Any) -> None:
        with self._lock:
            changed = False
            for key, value in kwargs.items():
                field_name = {
                    "ota_secret": "global_ota_secret",
                    "auth_token": "global_auth_token",
                }.get(key)
                if field_name is None or value is None:
                    continue
                text = str(value).strip()
                if not text:
                    continue
                if not str(getattr(self._data, field_name, "") or "").strip():
                    setattr(self._data, field_name, text)
                    changed = True
            if changed:
                self._save_unlocked()

    def migrate_legacy_settings(self, legacy: BridgeSettings) -> None:
        ip = legacy.device_ip.strip()
        if not ip:
            return
        with self._lock:
            if self._data.devices:
                return
            uid, _, fw = fetch_device_uid(ip, legacy.auth_token)
            uid = _normalize_uid(uid, ip)
            now = _now()
            self._data.devices[uid] = PairedDevice(
                device_uid=uid,
                ip=ip,
                ota_secret=legacy.ota_secret.strip(),
                auth_token=legacy.auth_token.strip(),
                firmware_version=fw,
                last_seen_at=now,
                paired_at=now,
                source="manual",
            )
            self._data.active_device_uid = uid
            if legacy.ota_secret.strip() and not self._data.global_ota_secret:
                self._data.global_ota_secret = legacy.ota_secret.strip()
            if legacy.auth_token.strip() and not self._data.global_auth_token:
                self._data.global_auth_token = legacy.auth_token.strip()
            self._save_unlocked()

    def seed_manual_device(
        self,
        *,
        ip: str,
        device_uid: str = "",
        device_name: str = "",
        ota_secret: str = "",
        auth_token: str = "",
    ) -> PairedDevice | None:
        ip = str(ip or "").strip()
        if not ip:
            return None
        uid = str(device_uid or "").strip()
        fw = ""
        if not uid:
            uid, _, fw = fetch_device_uid(ip, auth_token)
        uid = _normalize_uid(uid, ip)
        now = _now()
        with self._lock:
            existing = self._data.devices.get(uid)
            device = PairedDevice(
                device_uid=uid,
                device_name=device_name or (existing.device_name if existing else ""),
                ip=ip,
                ota_secret=ota_secret or (existing.ota_secret if existing else ""),
                auth_token=auth_token or (existing.auth_token if existing else ""),
                firmware_version=fw or (existing.firmware_version if existing else ""),
                last_seen_at=now,
                paired_at=existing.paired_at if existing else now,
                source="manual",
            )
            self._data.devices[uid] = device
            if not self._data.active_device_uid:
                self._data.active_device_uid = uid
            self._save_unlocked()
        self.ensure_auth_token(uid)
        return self.get(uid)

    def note_seen(
        self,
        *,
        device_uid: str,
        device_name: str = "",
        ip: str = "",
        firmware_version: str = "",
    ) -> PairedDevice | None:
        ip = str(ip or "").strip()
        if not ip:
            return None
        uid = _normalize_uid(device_uid, ip)
        now = _now()
        with self._lock:
            existing = self._data.devices.get(uid)
            device = PairedDevice(
                device_uid=uid,
                device_name=device_name or (existing.device_name if existing else ""),
                ip=ip,
                ota_secret=existing.ota_secret if existing else "",
                auth_token=existing.auth_token if existing else "",
                firmware_version=firmware_version or (existing.firmware_version if existing else ""),
                last_seen_at=now,
                paired_at=existing.paired_at if existing else now,
                source=existing.source if existing else "auto",
            )
            self._data.devices[uid] = device
            if not self._data.active_device_uid:
                self._data.active_device_uid = uid
            self._save_unlocked()
        self.ensure_auth_token(uid)
        return self.get(uid)

    def list_devices(self) -> list[PairedDevice]:
        with self._lock:
            devices = [PairedDevice.from_dict(d.to_dict()) for d in self._data.devices.values()]
        devices.sort(key=lambda d: d.last_seen_at, reverse=True)
        return devices

    def get(self, device_uid: str) -> PairedDevice | None:
        uid = str(device_uid or "").strip()
        if not uid:
            return None
        with self._lock:
            device = self._data.devices.get(uid)
            return PairedDevice.from_dict(device.to_dict()) if device else None

    def active_device(self) -> PairedDevice | None:
        with self._lock:
            uid = self._data.active_device_uid.strip()
            if not uid:
                return None
            device = self._data.devices.get(uid)
            return PairedDevice.from_dict(device.to_dict()) if device else None

    def set_active(self, device_uid: str) -> bool:
        uid = str(device_uid or "").strip()
        with self._lock:
            if uid not in self._data.devices:
                return False
            self._data.active_device_uid = uid
            self._save_unlocked()
            return True

    def resolve_ip(self, device_uid: str, fallback_ip: str = "") -> str:
        device = self.get(device_uid)
        if device and device.ip.strip():
            return device.ip.strip()
        return str(fallback_ip or "").strip()

    def update_active(self, **kwargs: Any) -> PairedDevice | None:
        with self._lock:
            uid = self._data.active_device_uid.strip()
            if not uid or uid not in self._data.devices:
                return None
            device = self._data.devices[uid]
            data = device.to_dict()
            for key, value in kwargs.items():
                if key in data and value is not None:
                    data[key] = str(value)
            self._data.devices[uid] = PairedDevice.from_dict(data)
            self._save_unlocked()
            return PairedDevice.from_dict(data)

    def upsert_device(self, device_uid: str, **kwargs: Any) -> PairedDevice | None:
        uid = str(device_uid or "").strip()
        if not uid:
            return None
        now = _now()
        with self._lock:
            existing = self._data.devices.get(uid)
            data = (existing or PairedDevice(device_uid=uid, paired_at=now)).to_dict()
            for key, value in kwargs.items():
                if key in data and value is not None:
                    data[key] = value if key in ("last_seen_at", "paired_at") else str(value)
            if not data.get("paired_at"):
                data["paired_at"] = now
            device = PairedDevice.from_dict(data)
            self._data.devices[uid] = device
            if not self._data.active_device_uid:
                self._data.active_device_uid = uid
            self._save_unlocked()
            return PairedDevice.from_dict(device.to_dict())

    def remove_device(self, device_uid: str) -> bool:
        uid = str(device_uid or "").strip()
        with self._lock:
            if uid not in self._data.devices:
                return False
            del self._data.devices[uid]
            if self._data.active_device_uid == uid:
                self._data.active_device_uid = next(iter(self._data.devices), "")
            self._save_unlocked()
            return True

    def active_settings(self) -> BridgeSettings:
        with self._lock:
            uid = self._data.active_device_uid.strip()
        return self.device_settings(uid)

    def device_settings(self, device_uid: str = "") -> BridgeSettings:
        with self._lock:
            uid = str(device_uid or self._data.active_device_uid or "").strip()
            device = self._data.devices.get(uid) if uid else None
            global_ota = self._data.global_ota_secret.strip()
            global_auth = self._data.global_auth_token.strip()
            if device is None:
                return BridgeSettings(ota_secret=global_ota, auth_token=global_auth)
            token = device.auth_token.strip() or global_auth
            return BridgeSettings(
                device_ip=device.ip.strip(),
                ota_secret=device.ota_secret.strip() or global_ota,
                auth_token=token,
            )

    def cache_config_secrets(self, device_uid: str, patch: dict[str, Any]) -> None:
        uid = str(device_uid or "").strip()
        if not uid or not isinstance(patch, dict):
            return
        updates: dict[str, str] = {}
        wifi = patch.get("wifi")
        if isinstance(wifi, dict):
            password = str(wifi.get("password", "") or "")
            if password and password != "***":
                updates["wifi_password"] = password
        auth = patch.get("auth")
        if isinstance(auth, dict):
            token = str(auth.get("token", "") or "")
            if token and token != "***":
                updates["auth_token"] = token
        ota = patch.get("ota")
        if isinstance(ota, dict):
            secret = str(ota.get("secret", "") or "")
            if secret and secret != "***":
                updates["ota_secret"] = secret
        if updates:
            self.upsert_device(uid, **updates)

    def secrets_for_device(self, device_uid: str) -> dict[str, str]:
        device = self.get(device_uid)
        if device is None:
            return {}
        return {
            "wifi.password": device.wifi_password,
            "auth.token": device.auth_token,
            "ota.secret": device.ota_secret,
        }

    def devices_payload(self) -> dict[str, Any]:
        active = self.active_device()
        active_uid = active.device_uid if active else ""
        return {
            "active_device_uid": active_uid,
            "devices": [
                {
                    "device_uid": d.device_uid,
                    "device_name": d.device_name,
                    "ip": d.ip,
                    "firmware_version": d.firmware_version,
                    "last_seen_at": d.last_seen_at,
                    "paired_at": d.paired_at,
                    "source": d.source,
                    "active": d.device_uid == active_uid,
                }
                for d in self.list_devices()
            ],
        }

    def _copy_data(self) -> DeviceRegistryData:
        return DeviceRegistryData(
            schema_version=self._data.schema_version,
            active_device_uid=self._data.active_device_uid,
            global_ota_secret=self._data.global_ota_secret,
            global_auth_token=self._data.global_auth_token,
            devices={
                uid: PairedDevice.from_dict(device.to_dict())
                for uid, device in self._data.devices.items()
            },
        )

    def _from_file_dict(self, raw: dict[str, Any]) -> DeviceRegistryData:
        devices: dict[str, PairedDevice] = {}
        for item in raw.get("devices", []):
            if isinstance(item, dict):
                device = PairedDevice.from_dict(item)
                if device.device_uid:
                    devices[device.device_uid] = device
        return DeviceRegistryData(
            schema_version=int(raw.get("schema_version", 1) or 1),
            active_device_uid=str(raw.get("active_device_uid", "") or ""),
            global_ota_secret=str(raw.get("global_ota_secret", "") or ""),
            global_auth_token=str(raw.get("global_auth_token", "") or ""),
            devices=devices,
        )

    def _to_file_dict(self) -> dict[str, Any]:
        return {
            "schema_version": self._data.schema_version,
            "active_device_uid": self._data.active_device_uid,
            "global_ota_secret": self._data.global_ota_secret,
            "global_auth_token": self._data.global_auth_token,
            "devices": [device.to_dict() for device in self._data.devices.values()],
        }

    def _save_unlocked(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._path.write_text(json.dumps(self._to_file_dict(), indent=2) + "\n", encoding="utf-8")
