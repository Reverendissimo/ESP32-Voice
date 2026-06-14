"""Web admin UI and proxy routes for Hermes-Voice-Bridge."""

from __future__ import annotations

import cgi
import json
from pathlib import Path
from typing import Any, Callable
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from bridge_settings import BridgeSettings
from device_registry import DeviceRegistry, fetch_device_uid
from firmware_catalog import FirmwareCatalog
from firmware_hosting import FirmwareBundle

_ADMIN_HTML_PATH = Path(__file__).resolve().parent / "static" / "admin.html"
_MASKED = "***"


def load_admin_html() -> bytes:
    return _ADMIN_HTML_PATH.read_bytes()


def _device_base(settings: BridgeSettings) -> str | None:
    ip = settings.device_ip.strip()
    if not ip:
        return None
    return f"http://{ip}"


def _request_json(
    settings: BridgeSettings,
    method: str,
    path: str,
    *,
    body: dict[str, Any] | None = None,
    extra_headers: dict[str, str] | None = None,
    timeout: float = 15.0,
) -> tuple[int, dict[str, Any]]:
    base = _device_base(settings)
    if base is None:
        return 400, {"ok": False, "error": "device_ip not configured"}

    url = f"{base}{path}"
    headers = {"Accept": "application/json"}
    if settings.auth_token:
        headers["X-Auth-Token"] = settings.auth_token
    if extra_headers:
        headers.update(extra_headers)

    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = Request(url, data=data, headers=headers, method=method)
    try:
        with urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8")
            payload = json.loads(raw) if raw else {}
            if not isinstance(payload, dict):
                payload = {"raw": payload}
            return resp.status, payload
    except HTTPError as exc:
        try:
            payload = json.loads(exc.read().decode("utf-8"))
        except (json.JSONDecodeError, OSError):
            payload = {"error": exc.reason or str(exc)}
        if not isinstance(payload, dict):
            payload = {"error": str(payload)}
        return exc.code, payload
    except URLError as exc:
        return 502, {"ok": False, "error": str(exc.reason)}
    except json.JSONDecodeError as exc:
        return 502, {"ok": False, "error": f"invalid JSON from device: {exc}"}


def _enrich_config_secrets(
    payload: dict[str, Any],
    settings: BridgeSettings,
    *,
    device_registry: DeviceRegistry | None = None,
    device_uid: str = "",
) -> dict[str, Any]:
    """Fill masked placeholders with bridge-known secrets for the admin UI."""
    config = payload.get("config")
    if not isinstance(config, dict):
        return payload

    cached = device_registry.secrets_for_device(device_uid) if device_registry else {}

    auth = config.get("auth")
    if isinstance(auth, dict):
        if auth.get("token") in (_MASKED, "", None):
            value = cached.get("auth.token") or settings.auth_token
            if value:
                auth["token"] = value

    wifi = config.get("wifi")
    if isinstance(wifi, dict):
        if wifi.get("password") in (_MASKED, "", None):
            value = cached.get("wifi.password", "")
            if value:
                wifi["password"] = value

    ota = config.get("ota")
    if isinstance(ota, dict):
        if ota.get("secret") in (_MASKED, "", None):
            value = cached.get("ota.secret") or settings.ota_secret
            if value:
                ota["secret"] = value

    return payload


def _strip_masked_secrets(patch: Any) -> Any:
    if isinstance(patch, dict):
        out: dict[str, Any] = {}
        for key, value in patch.items():
            cleaned = _strip_masked_secrets(value)
            if cleaned == _MASKED:
                continue
            out[key] = cleaned
        return out
    if isinstance(patch, list):
        return [_strip_masked_secrets(item) for item in patch if item != _MASKED]
    return patch


class BridgeUiHandlers:
    def __init__(
        self,
        device_registry: DeviceRegistry,
        *,
        firmware_catalog: FirmwareCatalog,
        uploads_dir: Path,
        public_base_url_resolver: Callable[[], str],
        last_device_ip_getter: Callable[[], str] | None = None,
        echo_auth_token_getter: Callable[[], str] | None = None,
    ) -> None:
        self.device_registry = device_registry
        self.firmware_catalog = firmware_catalog
        self.uploads_dir = uploads_dir
        self.uploads_dir.mkdir(parents=True, exist_ok=True)
        self._public_base_url_resolver = public_base_url_resolver
        self._last_device_ip_getter = last_device_ip_getter
        self._echo_auth_token_getter = echo_auth_token_getter

    def resolve_firmware(self) -> FirmwareBundle | None:
        return self.firmware_catalog.active_bundle()

    def _firmware_info_payload(self) -> dict[str, Any]:
        return self.firmware_catalog.list_payload(self._public_base_url_resolver())

    def _ensure_settings(self) -> BridgeSettings:
        if self._echo_auth_token_getter is not None:
            self.device_registry.apply_global_defaults(auth_token=self._echo_auth_token_getter())
        last_ip = self._last_device_ip_getter() if self._last_device_ip_getter is not None else ""
        active = self.device_registry.active_device()
        if last_ip:
            if active is None:
                self.device_registry.seed_manual_device(ip=last_ip)
            elif not active.ip.strip():
                self.device_registry.update_active(ip=last_ip)
        active = self.device_registry.active_device()
        if active:
            self.device_registry.ensure_auth_token(active.device_uid)
        return self.device_registry.active_settings()

    def _device_auth_token_missing(self, config_body: dict[str, Any]) -> bool:
        config = config_body.get("config")
        if not isinstance(config, dict):
            return True
        auth = config.get("auth")
        if not isinstance(auth, dict):
            return True
        token = str(auth.get("token", "") or "")
        return token in ("", _MASKED)

    def _provision_device_auth(
        self,
        device_uid: str,
        settings: BridgeSettings,
        config_body: dict[str, Any],
    ) -> tuple[dict[str, Any], bool]:
        token = self.device_registry.ensure_auth_token(device_uid)
        if not token or not self._device_auth_token_missing(config_body):
            return config_body, False

        unauth = BridgeSettings(
            device_ip=settings.device_ip,
            ota_secret=settings.ota_secret,
            auth_token="",
        )
        patch = {"auth": {"token": token}}
        patch_status, patch_body = _request_json(
            unauth, "POST", "/api/v1/config/patch", body=patch
        )
        if patch_status >= 400:
            return config_body, False

        authed = BridgeSettings(
            device_ip=settings.device_ip,
            ota_secret=settings.ota_secret,
            auth_token=token,
        )
        _request_json(authed, "POST", "/api/v1/config/save", body={})
        self.device_registry.cache_config_secrets(device_uid, patch)

        config_status, fresh_config = _request_json(authed, "GET", "/api/v1/config")
        if config_status == 200:
            return self._enrich_config(fresh_config, authed), True
        return config_body, True

    def _enrich_config(self, payload: dict[str, Any], settings: BridgeSettings) -> dict[str, Any]:
        active = self.device_registry.active_device()
        return _enrich_config_secrets(
            payload,
            settings,
            device_registry=self.device_registry,
            device_uid=active.device_uid if active else "",
        )

    def _bootstrap_payload(self) -> dict[str, Any]:
        settings = self._ensure_settings()
        payload = self._settings_payload()
        payload.update(self.device_registry.devices_payload())
        payload["last_seen_device_ip"] = (
            self._last_device_ip_getter() if self._last_device_ip_getter is not None else ""
        )
        payload["firmware_info"] = self._firmware_info_payload()

        device_block: dict[str, Any] = {"reachable": False}
        active = self.device_registry.active_device()
        if active is None:
            device_block["error"] = (
                "No paired devices — add one below, set ESP_DEVICE_IP, or wait for speech upload"
            )
            payload["device"] = device_block
            return payload

        if not settings.device_ip.strip():
            device_block["error"] = "Active device has no IP — set it in Connection and save"
            payload["device"] = device_block
            return payload

        version_status, version_body = _request_json(settings, "GET", "/api/v1/version")
        device_block["version_status"] = version_status
        device_block["version"] = version_body
        if version_status == 200:
            device_block["reachable"] = True
            fw = str(version_body.get("firmware_version", "") or "")
            uid = str(version_body.get("device_uid", "") or "")
            if uid:
                old_uid = active.device_uid
                old = self.device_registry.get(old_uid)
                self.device_registry.note_seen(
                    device_uid=uid,
                    device_name=old.device_name if old else "",
                    ip=settings.device_ip,
                    firmware_version=fw,
                )
                if old and old_uid != uid:
                    self.device_registry.upsert_device(
                        uid,
                        ota_secret=old.ota_secret,
                        auth_token=old.auth_token,
                    )
                    self.device_registry.remove_device(old_uid)
                self.device_registry.set_active(uid)
            elif fw:
                self.device_registry.upsert_device(active.device_uid, firmware_version=fw)

        config_status, raw_config_body = _request_json(settings, "GET", "/api/v1/config")
        config_body = raw_config_body
        if config_status == 200 and active:
            config_body, provisioned = self._provision_device_auth(
                active.device_uid, settings, raw_config_body
            )
            if provisioned:
                settings = self.device_registry.device_settings(active.device_uid)
                device_block["auth_provisioned"] = True
        config_body = self._enrich_config(config_body, settings)
        device_block["auth_token"] = settings.auth_token
        device_block["config_status"] = config_status
        device_block["config"] = config_body
        if config_status != 200 and not device_block.get("error"):
            device_block["error"] = config_body.get("error") or f"config HTTP {config_status}"

        payload["device"] = device_block
        return payload

    def _apply_firmware(self, parsed: dict[str, Any]) -> dict[str, Any]:
        version = str(parsed.get("version", "") or parsed.get("firmware_version", "") or "").strip()
        if not version:
            return {"ok": False, "error": "version required"}
        bundle = self.firmware_catalog.bundle_for_version(version)
        if bundle is None:
            return {"ok": False, "error": f"firmware {version} not found on server"}
        base = self._public_base_url_resolver()
        firmware_url = self.firmware_catalog.binary_url(version, base)
        force = bool(parsed.get("force"))
        apply_all = bool(parsed.get("all_devices"))

        targets: list[str] = []
        if apply_all:
            targets = [d.device_uid for d in self.device_registry.list_devices()]
            if not targets:
                return {"ok": False, "error": "no paired devices"}
        else:
            active = self.device_registry.active_device()
            uid = str(parsed.get("device_uid", "") or (active.device_uid if active else "")).strip()
            if not uid:
                return {"ok": False, "error": "no device selected"}
            targets = [uid]

        results: list[dict[str, Any]] = []
        ok_count = 0
        for uid in targets:
            settings = self.device_registry.device_settings(uid)
            if not settings.device_ip.strip():
                results.append(
                    {"device_uid": uid, "ok": False, "error": "device has no IP configured"}
                )
                continue
            headers: dict[str, str] = {}
            if settings.ota_secret:
                headers["X-Ota-Secret"] = settings.ota_secret
            ota_body: dict[str, Any] = {"url": firmware_url}
            if force:
                ota_body["force"] = True
            status, payload = _request_json(
                settings,
                "POST",
                "/api/v1/ota/update",
                body=ota_body,
                extra_headers=headers,
                timeout=30.0,
            )
            entry = {
                "device_uid": uid,
                "device_ip": settings.device_ip,
                "http_status": status,
                "ok": status < 400,
                **payload,
            }
            if entry["ok"]:
                ok_count += 1
            results.append(entry)

        return {
            "ok": ok_count > 0,
            "version": version,
            "firmware_url": firmware_url,
            "manifest_url": self.firmware_catalog.manifest_url(version, base),
            "applied": ok_count,
            "total": len(targets),
            "results": results,
        }

    def handle_get(self, path: str) -> tuple[int, str, bytes] | None:
        if path in ("/", "/ui", "/ui/"):
            return 200, "text/html; charset=utf-8", load_admin_html()

        if path == "/ui/settings":
            return self._json_response(self._settings_payload())

        if path == "/ui/devices":
            payload = {"ok": True, **self.device_registry.devices_payload()}
            return self._json_response(payload)

        if path == "/ui/bootstrap":
            return self._json_response(self._bootstrap_payload())

        settings = self._ensure_settings()

        if path == "/ui/device/config":
            status, payload = _request_json(settings, "GET", "/api/v1/config")
            payload = self._enrich_config(payload, settings)
            return self._json_response(payload, status)

        if path == "/ui/device/config/saved":
            status, payload = _request_json(settings, "GET", "/api/v1/config/saved")
            payload = self._enrich_config(payload, settings)
            return self._json_response(payload, status)

        if path == "/ui/device/version":
            status, payload = _request_json(settings, "GET", "/api/v1/version")
            return self._json_response(payload, status)

        if path == "/ui/device/ota/status":
            headers = {}
            if settings.ota_secret:
                headers["X-Ota-Secret"] = settings.ota_secret
            status, payload = _request_json(
                settings, "GET", "/api/v1/ota/status", extra_headers=headers
            )
            return self._json_response(payload, status)

        if path == "/ui/firmware/list":
            return self._json_response(self._firmware_info_payload())

        if path == "/ui/firmware/info":
            return self._json_response(self._firmware_info_payload())

        return None

    def handle_post(self, path: str, body: bytes, headers: Any) -> tuple[int, str, bytes] | None:
        if path == "/ui/settings":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            if not isinstance(parsed, dict):
                return self._json_response({"ok": False, "error": "expected object"}, 400)
            active = self.device_registry.active_device()
            if active is None and parsed.get("device_ip"):
                device = self.device_registry.seed_manual_device(
                    ip=str(parsed.get("device_ip", "")),
                    ota_secret=str(parsed.get("ota_secret", "") or ""),
                    auth_token=str(parsed.get("auth_token", "") or ""),
                )
                if device is None:
                    return self._json_response({"ok": False, "error": "device_ip required"}, 400)
                active = device
            else:
                auth_token = str(parsed.get("auth_token", "") or "").strip()
                active = self.device_registry.active_device()
                if active and not auth_token:
                    auth_token = self.device_registry.ensure_auth_token(active.device_uid)
                self.device_registry.update_active(
                    ip=parsed.get("device_ip"),
                    ota_secret=parsed.get("ota_secret"),
                    auth_token=auth_token or parsed.get("auth_token"),
                )
                active = self.device_registry.active_device()
            if active:
                settings = self.device_registry.device_settings(active.device_uid)
                _, raw_config = _request_json(settings, "GET", "/api/v1/config")
                self._provision_device_auth(active.device_uid, settings, raw_config)
            return self._json_response({"ok": True, **self._settings_payload()})

        if path == "/ui/devices/select":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            if not isinstance(parsed, dict):
                return self._json_response({"ok": False, "error": "expected object"}, 400)
            uid = str(parsed.get("device_uid", "") or "").strip()
            if not uid or not self.device_registry.set_active(uid):
                return self._json_response({"ok": False, "error": "unknown device_uid"}, 404)
            return self._json_response({"ok": True, **self._bootstrap_payload()})

        if path == "/ui/devices":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            if not isinstance(parsed, dict):
                return self._json_response({"ok": False, "error": "expected object"}, 400)
            ip = str(parsed.get("ip", "") or "").strip()
            if not ip:
                return self._json_response({"ok": False, "error": "ip required"}, 400)
            uid = str(parsed.get("device_uid", "") or "").strip()
            auth_token = str(parsed.get("auth_token", "") or "")
            if not uid:
                uid, _, _ = fetch_device_uid(ip, auth_token)
            device = self.device_registry.seed_manual_device(
                ip=ip,
                device_uid=uid,
                device_name=str(parsed.get("device_name", "") or ""),
                ota_secret=str(parsed.get("ota_secret", "") or ""),
                auth_token=auth_token,
            )
            if device is None:
                return self._json_response({"ok": False, "error": "failed to add device"}, 400)
            if parsed.get("make_active", True):
                self.device_registry.set_active(device.device_uid)
            return self._json_response({"ok": True, "device": device.to_dict(), **self.device_registry.devices_payload()})

        if path == "/ui/devices/remove":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            if not isinstance(parsed, dict):
                return self._json_response({"ok": False, "error": "expected object"}, 400)
            uid = str(parsed.get("device_uid", "") or "").strip()
            if not uid or not self.device_registry.remove_device(uid):
                return self._json_response({"ok": False, "error": "unknown device_uid"}, 404)
            return self._json_response({"ok": True, **self.device_registry.devices_payload()})

        if path == "/ui/firmware/select":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            if not isinstance(parsed, dict):
                return self._json_response({"ok": False, "error": "expected object"}, 400)
            version = str(parsed.get("version", "") or "").strip()
            if not version or not self.firmware_catalog.set_active(version):
                return self._json_response({"ok": False, "error": "unknown firmware version"}, 404)
            return self._json_response(self._firmware_info_payload())

        if path == "/ui/firmware/apply":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            if not isinstance(parsed, dict):
                return self._json_response({"ok": False, "error": "expected object"}, 400)
            return self._json_response(self._apply_firmware(parsed))

        settings = self._ensure_settings()

        if path == "/ui/device/config/patch":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                return self._json_response({"ok": False, "error": "invalid JSON"}, 400)
            patch = parsed.get("patch") if isinstance(parsed, dict) else parsed
            if not isinstance(patch, dict):
                return self._json_response({"ok": False, "error": "patch object required"}, 400)
            patch = _strip_masked_secrets(patch)
            status, payload = _request_json(
                settings, "POST", "/api/v1/config/patch", body=patch
            )
            if status < 400:
                active = self.device_registry.active_device()
                if active:
                    self.device_registry.cache_config_secrets(active.device_uid, patch)
            return self._json_response(payload, status)

        if path == "/ui/device/config/save":
            status, payload = _request_json(
                settings, "POST", "/api/v1/config/save", body={}
            )
            return self._json_response(payload, status)

        if path == "/ui/device/config/load":
            status, payload = _request_json(
                settings, "POST", "/api/v1/config/load", body={}
            )
            return self._json_response(payload, status)

        if path == "/ui/device/ota/trigger":
            try:
                parsed = json.loads(body.decode("utf-8") if body else "{}")
            except json.JSONDecodeError:
                parsed = {}
            if not isinstance(parsed, dict):
                parsed = {}
            headers_map = {}
            if settings.ota_secret:
                headers_map["X-Ota-Secret"] = settings.ota_secret
            ota_body: dict[str, Any] = {}
            if parsed.get("force"):
                ota_body["force"] = True
            manifest_url = str(
                parsed.get("url", "") or parsed.get("manifest_url", "") or ""
            ).strip()
            if manifest_url:
                if manifest_url.endswith("manifest.json"):
                    # Device expects a .bin URL, not the manifest JSON.
                    version = ""
                    for part in manifest_url.split("/"):
                        if part and part[0].isdigit():
                            version = part
                    if version:
                        manifest_url = self.firmware_catalog.binary_url(
                            version, self._public_base_url_resolver()
                        )
                ota_body["url"] = manifest_url
            status, payload = _request_json(
                settings,
                "POST",
                "/api/v1/ota/update",
                body=ota_body,
                extra_headers=headers_map,
                timeout=30.0,
            )
            return self._json_response(payload, status)

        if path == "/ui/firmware/upload":
            return None  # handled in server with streaming multipart parser

        return None

    def parse_firmware_upload(self, headers: Any, rfile: Any) -> tuple[bytes, str]:
        content_type = headers.get("Content-Type", "")
        if "multipart/form-data" not in content_type:
            raise ValueError("multipart/form-data required")
        length = int(headers.get("Content-Length", "0") or "0")
        environ = {
            "REQUEST_METHOD": "POST",
            "CONTENT_TYPE": content_type,
            "CONTENT_LENGTH": str(length),
        }
        fs = cgi.FieldStorage(fp=rfile, headers=headers, environ=environ)
        if "firmware" not in fs:
            raise ValueError("missing firmware file field")
        field = fs["firmware"]
        if isinstance(field, list):
            field = field[0]
        data = field.file.read() if field.file else b""
        version_override = ""
        if "version" in fs:
            version_field = fs["version"]
            if not isinstance(version_field, list):
                version_override = str(version_field.value or "")
        if not data:
            raise ValueError("empty firmware upload")
        return data, version_override

    def save_firmware_bytes(self, data: bytes, *, version_override: str = "") -> dict[str, Any]:
        entry = self.firmware_catalog.add_bytes(data, version_override=version_override)
        info = self._firmware_info_payload()
        return {
            "ok": True,
            "version": entry.version,
            "size": entry.size,
            "path": str(self.firmware_catalog.root_dir / entry.filename),
            **info,
        }

    def _settings_payload(self) -> dict[str, Any]:
        settings = self._ensure_settings()
        active = self.device_registry.active_device()
        if active:
            self.device_registry.ensure_auth_token(active.device_uid)
            settings = self.device_registry.device_settings(active.device_uid)
        fw = self._firmware_info_payload()
        return {
            "ok": True,
            "device_ip": settings.device_ip,
            "ota_secret": settings.ota_secret,
            "auth_token": settings.auth_token,
            "active_device_uid": active.device_uid if active else "",
            "active_device_name": active.device_name if active else "",
            "firmware": {
                "hosted": fw.get("hosted", False),
                "version": fw.get("version", ""),
                "size": fw.get("size", 0),
                "path": fw.get("path", ""),
                "manifest_url": fw.get("manifest_url", ""),
            },
        }

    @staticmethod
    def _json_response(payload: dict[str, Any], status: int = 200) -> tuple[int, str, bytes]:
        body = json.dumps(payload, indent=2).encode("utf-8")
        return status, "application/json; charset=utf-8", body
