"""Catalog of firmware binaries hosted on the bridge for OTA."""

from __future__ import annotations

import json
import re
import shutil
import threading
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from firmware_hosting import FirmwareBundle, resolve_firmware_version

_SAFE_VERSION_RE = re.compile(r"[^A-Za-z0-9._-]+")


@dataclass
class FirmwareEntry:
    version: str
    filename: str
    size: int
    uploaded_at: float
    source: str = "upload"

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> FirmwareEntry:
        return cls(
            version=str(data.get("version", "") or ""),
            filename=str(data.get("filename", "") or ""),
            size=int(data.get("size", 0) or 0),
            uploaded_at=float(data.get("uploaded_at", 0.0) or 0.0),
            source=str(data.get("source", "upload") or "upload"),
        )


def _safe_version(version: str) -> str:
    cleaned = _SAFE_VERSION_RE.sub("_", str(version or "").strip())
    return cleaned or "unknown"


def _now() -> float:
    return time.time()


class FirmwareCatalog:
    def __init__(self, root_dir: Path) -> None:
        self.root_dir = root_dir
        self.catalog_path = root_dir / "catalog.json"
        self._lock = threading.Lock()
        self._active_version = ""
        self._entries: dict[str, FirmwareEntry] = {}
        self.root_dir.mkdir(parents=True, exist_ok=True)
        self.load()
        self._scan_disk_unlocked()
        self._migrate_legacy_unlocked()

    def load(self) -> None:
        with self._lock:
            if self.catalog_path.is_file():
                try:
                    raw = json.loads(self.catalog_path.read_text(encoding="utf-8"))
                    if isinstance(raw, dict):
                        self._active_version = str(raw.get("active_version", "") or "")
                        entries: dict[str, FirmwareEntry] = {}
                        for item in raw.get("entries", []):
                            if isinstance(item, dict):
                                entry = FirmwareEntry.from_dict(item)
                                if entry.version and entry.filename:
                                    entries[entry.version] = entry
                        self._entries = entries
                except (OSError, json.JSONDecodeError):
                    pass

    def list_entries(self) -> list[FirmwareEntry]:
        with self._lock:
            entries = [FirmwareEntry.from_dict(e.to_dict()) for e in self._entries.values()]
        entries.sort(key=lambda e: e.uploaded_at, reverse=True)
        return entries

    def active_version(self) -> str:
        with self._lock:
            return self._active_version

    def set_active(self, version: str) -> bool:
        ver = str(version or "").strip()
        with self._lock:
            if ver not in self._entries:
                return False
            self._active_version = ver
            self._save_unlocked()
            return True

    def active_bundle(self) -> FirmwareBundle | None:
        with self._lock:
            ver = self._active_version.strip()
            entry = self._entries.get(ver) if ver else None
            if entry is None and self._entries:
                entry = next(iter(sorted(self._entries.values(), key=lambda e: e.uploaded_at, reverse=True)))
                self._active_version = entry.version
            if entry is None:
                return None
            path = self.root_dir / entry.filename
        if not path.is_file():
            return None
        stat = path.stat()
        return FirmwareBundle(
            bin_path=path,
            version=entry.version,
            size=stat.st_size,
            mtime_ns=stat.st_mtime_ns,
        )

    def bundle_for_version(self, version: str) -> FirmwareBundle | None:
        ver = str(version or "").strip()
        if not ver:
            return self.active_bundle()
        with self._lock:
            entry = self._entries.get(ver)
            if entry is None:
                return None
            path = self.root_dir / entry.filename
        if not path.is_file():
            return None
        stat = path.stat()
        return FirmwareBundle(
            bin_path=path,
            version=entry.version,
            size=stat.st_size,
            mtime_ns=stat.st_mtime_ns,
        )

    def add_bytes(self, data: bytes, *, version_override: str = "", source: str = "upload") -> FirmwareEntry:
        if len(data) < 1024:
            raise ValueError("firmware file too small")
        tmp = self.root_dir / ".upload.tmp.bin"
        tmp.write_bytes(data)
        try:
            version = resolve_firmware_version(tmp, override=version_override or None)
            return self._register_file(tmp, version=version, source=source, move=True)
        finally:
            if tmp.is_file():
                tmp.unlink(missing_ok=True)

    def import_file(self, path: Path, *, version_override: str = "", source: str = "import") -> FirmwareEntry | None:
        src = Path(path)
        if not src.is_file():
            return None
        version = resolve_firmware_version(src, override=version_override or None)
        dest = self.root_dir / f"esp32-voice-{_safe_version(version)}.bin"
        shutil.copy2(src, dest)
        return self._register_file(dest, version=version, source=source, move=False)

    def import_dist_default(self) -> FirmwareEntry | None:
        dist_bin = Path(__file__).resolve().parents[2] / "dist" / "esp32-voice-flash" / "esp32-voice.bin"
        if not dist_bin.is_file():
            return None
        version = resolve_firmware_version(dist_bin)
        with self._lock:
            if version in self._entries:
                return FirmwareEntry.from_dict(self._entries[version].to_dict())
        return self.import_file(dist_bin, source="dist")

    def manifest_url(self, version: str, base_url: str) -> str:
        ver = str(version or "").strip() or self.active_version()
        base = base_url.rstrip("/")
        if ver:
            return f"{base}/firmware/{ver}/manifest.json"
        return f"{base}/firmware/manifest.json"

    def binary_url(self, version: str, base_url: str) -> str:
        ver = str(version or "").strip() or self.active_version()
        base = base_url.rstrip("/")
        if ver:
            return f"{base}/firmware/{ver}/esp32-voice.bin"
        return f"{base}/firmware/esp32-voice.bin"

    def list_payload(self, base_url: str) -> dict[str, Any]:
        active = self.active_version()
        firmwares = []
        for entry in self.list_entries():
            firmwares.append(
                {
                    **entry.to_dict(),
                    "active": entry.version == active,
                    "manifest_url": self.manifest_url(entry.version, base_url),
                    "binary_url": self.binary_url(entry.version, base_url),
                }
            )
        bundle = self.active_bundle()
        return {
            "ok": True,
            "active_version": active,
            "firmwares": firmwares,
            "hosted": bundle is not None,
            "version": bundle.version if bundle else "",
            "size": bundle.size if bundle else 0,
            "path": str(bundle.bin_path) if bundle else "",
            "manifest_url": self.manifest_url(active, base_url) if active else "",
            "public_base_url": base_url,
        }

    def _register_file(
        self,
        path: Path,
        *,
        version: str,
        source: str,
        move: bool,
    ) -> FirmwareEntry:
        ver = str(version or "").strip() or "0.0.0"
        filename = f"esp32-voice-{_safe_version(ver)}.bin"
        dest = self.root_dir / filename
        if move:
            if dest.is_file():
                dest.unlink()
            path.replace(dest)
        elif path.resolve() != dest.resolve():
            shutil.copy2(path, dest)
        stat = dest.stat()
        entry = FirmwareEntry(
            version=ver,
            filename=filename,
            size=stat.st_size,
            uploaded_at=_now(),
            source=source,
        )
        with self._lock:
            self._entries[ver] = entry
            self._active_version = ver
            self._save_unlocked()
        return FirmwareEntry.from_dict(entry.to_dict())

    def _scan_disk_unlocked(self) -> None:
        changed = False
        for path in sorted(self.root_dir.glob("esp32-voice*.bin")):
            if path.name.startswith("."):
                continue
            version = resolve_firmware_version(path)
            if path.name == "esp32-voice.bin" and version:
                version = version
            existing = self._entries.get(version)
            if existing and (self.root_dir / existing.filename).is_file():
                continue
            stat = path.stat()
            filename = path.name
            if path.name == "esp32-voice.bin":
                filename = f"esp32-voice-{_safe_version(version)}.bin"
                target = self.root_dir / filename
                if not target.is_file():
                    shutil.copy2(path, target)
            self._entries[version] = FirmwareEntry(
                version=version,
                filename=filename,
                size=stat.st_size,
                uploaded_at=stat.st_mtime,
                source="scan",
            )
            if not self._active_version:
                self._active_version = version
            changed = True
        if changed:
            self._save_unlocked()

    def _migrate_legacy_unlocked(self) -> None:
        legacy = self.root_dir.parent / "esp32-voice.bin"
        if not legacy.is_file():
            return
        version = resolve_firmware_version(legacy)
        if version in self._entries:
            return
        filename = f"esp32-voice-{_safe_version(version)}.bin"
        dest = self.root_dir / filename
        if not dest.is_file():
            shutil.copy2(legacy, dest)
        stat = dest.stat()
        self._entries[version] = FirmwareEntry(
            version=version,
            filename=filename,
            size=stat.st_size,
            uploaded_at=stat.st_mtime,
            source="legacy",
        )
        if not self._active_version:
            self._active_version = version
        self._save_unlocked()

    def _save_unlocked(self) -> None:
        payload = {
            "active_version": self._active_version,
            "entries": [entry.to_dict() for entry in self._entries.values()],
        }
        self.catalog_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
