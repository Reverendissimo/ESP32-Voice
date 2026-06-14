"""Host OTA firmware manifest and binary for ESP32 devices."""

from __future__ import annotations

import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

_ESP_APP_DESC_MAGIC = 0xABCD5432
_VERSION_TXT_RE = re.compile(r"esp32-voice\s+(\S+)")


@dataclass(frozen=True)
class FirmwareBundle:
    bin_path: Path
    version: str
    size: int
    mtime_ns: int


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_firmware_bin(explicit: str | None = None) -> Path | None:
    if explicit:
        path = Path(explicit)
        return path if path.is_file() else None

    root = repo_root()
    candidates = (
        root / "dist" / "esp32-voice-flash" / "esp32-voice.bin",
        root / "firmware" / "build" / "esp32-voice.bin",
    )
    for path in candidates:
        if path.is_file():
            return path
    return None


def read_version_from_binary(bin_path: Path) -> str | None:
    data = bin_path.read_bytes()
    needle = struct.pack("<I", _ESP_APP_DESC_MAGIC)
    start = 0
    while True:
        idx = data.find(needle, start)
        if idx < 0 or idx + 48 > len(data):
            return None
        version = data[idx + 16 : idx + 48].split(b"\x00", 1)[0].decode("ascii", errors="ignore").strip()
        if version:
            return version
        start = idx + 4


def read_version_from_version_txt(bin_path: Path) -> str | None:
    version_txt = bin_path.parent / "VERSION.txt"
    if not version_txt.is_file():
        return None
    for line in version_txt.read_text(encoding="utf-8").splitlines():
        match = _VERSION_TXT_RE.match(line.strip())
        if match:
            return match.group(1)
    return None


def read_version_from_cmake() -> str | None:
    cmake_lists = repo_root() / "firmware" / "CMakeLists.txt"
    if not cmake_lists.is_file():
        return None
    for line in cmake_lists.read_text(encoding="utf-8").splitlines():
        if "project(esp32-voice VERSION" in line:
            return line.split("VERSION", 1)[1].strip().strip(")").strip()
    return None


def resolve_firmware_version(bin_path: Path, *, override: str | None = None) -> str:
    if override:
        return override.strip()
    for version in (
        read_version_from_version_txt(bin_path),
        read_version_from_binary(bin_path),
        read_version_from_cmake(),
    ):
        if version:
            return version
    return "0.0.0"


def resolve_firmware_bundle(
    *,
    explicit_bin: str | None = None,
    version_override: str | None = None,
) -> FirmwareBundle | None:
    bin_path = resolve_firmware_bin(explicit_bin)
    if bin_path is None:
        return None
    stat = bin_path.stat()
    return FirmwareBundle(
        bin_path=bin_path,
        version=resolve_firmware_version(bin_path, override=version_override),
        size=stat.st_size,
        mtime_ns=stat.st_mtime_ns,
    )


def build_manifest(*, bundle: FirmwareBundle, base_url: str) -> dict:
    base = base_url.rstrip("/")
    return {
        "version": bundle.version,
        "url": f"{base}/firmware/esp32-voice.bin",
        "size": bundle.size,
        "project": "esp32-voice",
    }


def manifest_json(*, bundle: FirmwareBundle, base_url: str) -> bytes:
    payload = build_manifest(bundle=bundle, base_url=base_url)
    return (json.dumps(payload, indent=2) + "\n").encode("utf-8")
