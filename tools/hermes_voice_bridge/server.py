#!/usr/bin/env python3
"""
Hermes-Voice-Bridge — backend server for the ESP32-Voice terminal.

Connects ESP32 speech upload → faster-whisper ASR → Hermes agent → Chatterbox TTS → ESP playback.

Endpoints (under --prefix, default /api/v1):
  POST /speech/stream    - receive streamed PCM (A3 binary chunked HTTP)
  POST /speech           - receive PCM chunks (legacy A2 JSON + pcm_b64)
  POST /speech/finalize  - finalize utterance, save WAV, run ASR
  POST /tts/speak        - synthesize text with Chatterbox and play on ESP
  GET  /status           - recent utterances and saved recordings

On finalize, runs a final faster-whisper pass and prints:
  [asr] utt_N: full transcript

During upload, partial results print as audio arrives:
  [asr+] utt_N (en, 0.3s): new words...

After ASR finalize, transcript is sent to Hermes agent, then Chatterbox TTS plays the reply on the ESP:
  [hermes] utt_N: assistant reply
  [tts] utt_N: 2.10s -> espbox-... @ 192.168.100.217

Optional: --echo-on to play the raw recording back (off by default).
Optional: --no-hermes to skip LLM and speak the transcript directly.
Optional: --no-tts to disable Chatterbox synthesis.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import re
import sys
import threading
import time
import traceback
import wave
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

try:
    import requests
except ImportError:
    requests = None  # type: ignore

from bridge_log import blog, configure_bridge_log
from bridge_settings import BridgeSettingsStore, settings_defaults_from_env
from bridge_ui import BridgeUiHandlers, _UI_POST_PATHS
from device_registry import DeviceRegistry
from esp_playback import stream_pcm_to_esp
from firmware_catalog import FirmwareCatalog
from firmware_hosting import FirmwareBundle, manifest_json
from hermes_client import HermesClient, HermesError, build_hermes_from_args
from hermes_session import DEFAULT_SESSION_FILE, resolve_active_session
from ml_runtime import configure_ml_runtime
from transcriber import WhisperTranscriber, build_transcriber_from_args
from tts_engine import ChatterboxTtsEngine, build_tts_from_args


@dataclass
class Chunk:
    seq: int
    pcm: bytes
    sample_rate_hz: int
    channels: int


@dataclass
class Utterance:
    utterance_id: str
    session_id: str
    device_uid: str
    device_name: str
    chunks: dict[int, Chunk] = field(default_factory=dict)
    pcm: bytes = b""
    sample_rate_hz: int = 16000
    channels: int = 1
    protocol: str = "A3"
    started_at: str = field(
        default_factory=lambda: datetime.now(timezone.utc).isoformat()
    )


class SpeechStore:
    def __init__(self, recordings_dir: Path) -> None:
        self._lock = threading.Lock()
        self._active: dict[str, Utterance] = {}
        self._history: list[dict[str, Any]] = []
        self._last_device_ip = ""
        self.recordings_dir = recordings_dir
        self.recordings_dir.mkdir(parents=True, exist_ok=True)

    def note_device_ip(self, device_ip: str) -> None:
        ip = str(device_ip or "").strip()
        if not ip:
            return
        with self._lock:
            self._last_device_ip = ip

    @property
    def last_device_ip(self) -> str:
        with self._lock:
            return self._last_device_ip

    def add_chunk(self, payload: dict[str, Any]) -> None:
        utterance_id = str(payload["utterance_id"])
        pcm_b64 = payload.get("pcm_b64", "")
        pcm = base64.b64decode(pcm_b64, validate=True)
        chunk = Chunk(
            seq=int(payload.get("chunk_seq", 0)),
            pcm=pcm,
            sample_rate_hz=int(payload.get("sample_rate_hz", 16000)),
            channels=int(payload.get("channels", 1)),
        )

        with self._lock:
            utt = self._active.get(utterance_id)
            if utt is None:
                utt = Utterance(
                    utterance_id=utterance_id,
                    session_id=str(payload.get("session_id", "")),
                    device_uid=str(payload.get("device_uid", "")),
                    device_name=str(payload.get("device_name", "")),
                    protocol="A2",
                )
                self._active[utterance_id] = utt
            utt.chunks[chunk.seq] = chunk

    def begin_stream(
        self,
        *,
        utterance_id: str,
        session_id: str,
        device_uid: str,
        device_name: str,
        sample_rate_hz: int,
        channels: int,
    ) -> None:
        with self._lock:
            self._active[utterance_id] = Utterance(
                utterance_id=utterance_id,
                session_id=session_id,
                device_uid=device_uid,
                device_name=device_name,
                pcm=b"",
                sample_rate_hz=sample_rate_hz,
                channels=channels,
                protocol="A3",
            )

    def append_stream_pcm(self, utterance_id: str, pcm: bytes) -> None:
        if not pcm:
            return
        with self._lock:
            utt = self._active.get(utterance_id)
            if utt is None:
                return
            utt.pcm = utt.pcm + pcm

    def add_stream(
        self,
        *,
        utterance_id: str,
        session_id: str,
        device_uid: str,
        device_name: str,
        pcm: bytes,
        sample_rate_hz: int,
        channels: int,
    ) -> None:
        with self._lock:
            self._active[utterance_id] = Utterance(
                utterance_id=utterance_id,
                session_id=session_id,
                device_uid=device_uid,
                device_name=device_name,
                pcm=pcm,
                sample_rate_hz=sample_rate_hz,
                channels=channels,
                protocol="A3",
            )

    def finalize(self, payload: dict[str, Any]) -> dict[str, Any]:
        utterance_id = str(payload["utterance_id"])
        with self._lock:
            utt = self._active.pop(utterance_id, None)

        if utt is None:
            return {
                "ok": False,
                "error": "unknown utterance",
                "utterance_id": utterance_id,
            }

        if utt.pcm:
            pcm = utt.pcm
            sample_rate = utt.sample_rate_hz
            channels = utt.channels
            chunks_received = int(payload.get("chunk_count", 0))
        elif utt.chunks:
            ordered = [utt.chunks[k] for k in sorted(utt.chunks)]
            sample_rate = ordered[0].sample_rate_hz
            channels = ordered[0].channels
            pcm = b"".join(c.pcm for c in ordered)
            chunks_received = len(ordered)
        else:
            return {
                "ok": False,
                "error": "empty utterance",
                "utterance_id": utterance_id,
            }

        safe_id = "".join(c if c.isalnum() or c in "-_" else "_" for c in utterance_id)
        wav_path = self.recordings_dir / f"{safe_id}.wav"
        with wave.open(str(wav_path), "wb") as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm)

        duration_ms = int(payload.get("duration_ms", 0))
        chunk_count = int(payload.get("chunk_count", chunks_received))
        record = {
            "utterance_id": utterance_id,
            "session_id": utt.session_id,
            "device_uid": utt.device_uid,
            "device_name": utt.device_name,
            "protocol": utt.protocol,
            "chunk_count": chunk_count,
            "chunks_received": chunks_received,
            "duration_ms": duration_ms,
            "sample_rate_hz": sample_rate,
            "channels": channels,
            "pcm_bytes": len(pcm),
            "wav_path": str(wav_path),
            "finalized_at": datetime.now(timezone.utc).isoformat(),
        }

        with self._lock:
            self._history.insert(0, record)
            self._history = self._history[:20]

        blog(
            f"[speech] {utt.device_uid} {utterance_id} ({utt.protocol}): "
            f"{chunks_received} segments, {len(pcm)} bytes -> {wav_path}",
        )
        return {"ok": True, **record}

    def status(self) -> dict[str, Any]:
        with self._lock:
            active = [
                {
                    "utterance_id": u.utterance_id,
                    "device_uid": u.device_uid,
                    "protocol": u.protocol,
                    "chunks": len(u.chunks),
                    "pcm_bytes": len(u.pcm),
                }
                for u in self._active.values()
            ]
            history = list(self._history)
        return {
            "active_utterances": active,
            "recent": history,
            "recordings_dir": str(self.recordings_dir),
        }


def echo_playback(
    device_ip: str,
    device_uid: str,
    pcm: bytes,
    sample_rate_hz: int,
    channels: int,
    auth_token: str,
    *,
    chunk_bytes: int = 8184,
    stream_end: bool = True,
) -> bool:
    return stream_pcm_to_esp(
        device_ip,
        device_uid,
        pcm,
        sample_rate_hz,
        channels,
        auth_token,
        chunk_bytes=chunk_bytes,
        stream_end=stream_end,
        log_prefix="[echo]",
    )


def _split_pending_sentences(pending: str) -> tuple[list[str], str]:
    sentences: list[str] = []
    rest = pending
    while True:
        match = re.search(r"[.!?](?:\s+|$)", rest)
        if not match:
            break
        sentence = rest[: match.end()].strip()
        rest = rest[match.end() :]
        if len(sentence) >= 3:
            sentences.append(sentence)
    return sentences, rest


_MIN_TTS_CLAUSE_CHARS = 24
_MIN_TTS_FIRST_CLAUSE_CHARS = 12
_MIN_TTS_IDLE_FLUSH_CHARS = 20
_MAX_TTS_PENDING_CHARS = 100
_FIRST_CHUNK_WORD_CHARS = 40


def _split_pending_speech_chunks(
    pending: str,
    *,
    min_clause_chars: int = _MIN_TTS_CLAUSE_CHARS,
    first_clause_chars: int = _MIN_TTS_FIRST_CLAUSE_CHARS,
    max_pending_chars: int = _MAX_TTS_PENDING_CHARS,
    idle_min_chars: int = _MIN_TTS_IDLE_FLUSH_CHARS,
    aggressive_first: bool = False,
    idle_flush: bool = False,
) -> tuple[list[str], str]:
    """Extract speakable units: sentences, comma clauses, word buffers, or idle tail."""
    if idle_flush:
        text = pending.strip()
        if len(text) >= idle_min_chars:
            return [text], ""
        return [], pending

    chunks, rest = _split_pending_sentences(pending)
    clause_min = first_clause_chars if aggressive_first else min_clause_chars

    while True:
        match = re.search(r",\s+", rest)
        if not match:
            break
        clause = rest[: match.start()].strip()
        if len(clause) < clause_min:
            break
        chunks.append(clause)
        rest = rest[match.end() :]

    word_cut = _FIRST_CHUNK_WORD_CHARS if aggressive_first else max_pending_chars
    if len(rest) >= word_cut:
        cut = rest[:word_cut]
        last_space = cut.rfind(" ")
        if last_space >= clause_min:
            chunks.append(rest[:last_space].strip())
            rest = rest[last_space + 1 :]
    elif len(rest) >= max_pending_chars:
        cut = rest[:max_pending_chars]
        last_space = cut.rfind(" ")
        if last_space >= min_clause_chars:
            chunks.append(rest[:last_space].strip())
            rest = rest[last_space + 1 :]

    return chunks, rest


def hermes_failure_reply(kind: str) -> str:
    if kind == "timeout":
        return "Sorry, the assistant is taking too long. Please try again."
    if kind == "http":
        return "Sorry, the assistant had a problem. Please try again."
    return "Sorry, I could not reach the assistant right now."


def device_auth_token(cfg: "ServerConfig", device_uid: str) -> str:
    if cfg.device_registry is not None and device_uid:
        return cfg.device_registry.device_settings(device_uid).auth_token
    return cfg.echo_auth_token


def handle_voice_reply(
    cfg: "ServerConfig",
    utterance_id: str,
    user_text: str,
    *,
    device_uid: str,
    device_ip: str,
    session_id: str = "",
) -> None:
    reply = user_text
    if cfg.hermes is not None:
        stream_lock = threading.Lock()
        stream_stop = threading.Event()
        stream_state = {
            "pending": "",
            "tts_started": False,
            "first_chunk": True,
            "last_delta": 0.0,
        }

        def speak_chunks(chunks: list[str], *, idle: bool = False) -> None:
            if not chunks or cfg.tts is None:
                return
            auth = device_auth_token(cfg, device_uid)
            for chunk in chunks:
                tag = "hermes+ idle" if idle else "hermes+"
                blog(f"[{tag}] {utterance_id}: {chunk}")
                stream_state["tts_started"] = True
                cfg.tts.speak_async(
                    utterance_id,
                    chunk,
                    device_uid=device_uid,
                    device_ip=device_ip,
                    auth_token=auth,
                    stream_end=False,
                )

        def drain_pending(*, idle: bool = False) -> None:
            with stream_lock:
                pending = stream_state["pending"]
                if not pending.strip():
                    return
                if idle:
                    chunks, rest = _split_pending_speech_chunks(
                        pending,
                        idle_flush=True,
                        idle_min_chars=cfg.tts_idle_flush_min_chars,
                    )
                else:
                    chunks, rest = _split_pending_speech_chunks(
                        pending,
                        min_clause_chars=cfg.tts_min_clause_chars,
                        first_clause_chars=cfg.tts_first_clause_chars,
                        aggressive_first=stream_state["first_chunk"],
                    )
                stream_state["pending"] = rest
                if chunks:
                    stream_state["first_chunk"] = False
            speak_chunks(chunks, idle=idle)

        def on_delta(delta: str) -> None:
            with stream_lock:
                stream_state["pending"] += delta
                stream_state["last_delta"] = time.monotonic()
            drain_pending()

        def idle_watch() -> None:
            poll_s = max(0.02, cfg.tts_idle_poll_ms / 1000.0)
            flush_s = cfg.tts_idle_flush_ms / 1000.0
            while not stream_stop.wait(poll_s):
                if flush_s <= 0:
                    continue
                with stream_lock:
                    pending_ok = bool(stream_state["pending"].strip())
                    last_delta = stream_state["last_delta"]
                if not pending_ok or last_delta <= 0:
                    continue
                if time.monotonic() - last_delta < flush_s:
                    continue
                drain_pending(idle=True)
                with stream_lock:
                    stream_state["last_delta"] = time.monotonic()

        watcher = threading.Thread(
            target=idle_watch,
            name=f"tts-idle-{utterance_id}",
            daemon=True,
        )
        watcher.start()
        try:
            reply = cfg.hermes.chat(
                user_text,
                conversation_id=cfg.hermes_conversation_id,
                on_delta=on_delta,
            )
            if reply:
                blog(f"[hermes] {utterance_id}: {reply}")
            else:
                blog(f"[hermes] {utterance_id}: (empty reply)")
                reply = user_text
        except HermesError as exc:
            blog(f"[hermes] {utterance_id}: {exc.kind}: {exc}")
            reply = hermes_failure_reply(exc.kind)
        except Exception as exc:
            blog(f"[hermes] {utterance_id}: unexpected error: {exc}")
            traceback.print_exc()
            reply = hermes_failure_reply("unknown")
        else:
            with stream_lock:
                remainder = stream_state["pending"].strip()
                stream_state["pending"] = ""
                tts_started = stream_state["tts_started"]
            if remainder and cfg.tts is not None:
                cfg.tts.speak_async(
                    utterance_id,
                    remainder,
                    device_uid=device_uid,
                    device_ip=device_ip,
                    auth_token=device_auth_token(cfg, device_uid),
                    stream_end=True,
                )
            elif tts_started and cfg.tts is not None:
                cfg.tts.speak_stream_end(
                    utterance_id,
                    device_uid=device_uid,
                    device_ip=device_ip,
                    auth_token=device_auth_token(cfg, device_uid),
                )
            return
        finally:
            stream_stop.set()
            watcher.join(timeout=1.0)

    if cfg.tts is None or not reply:
        return

    cfg.tts.speak_async(
        utterance_id,
        reply,
        device_uid=device_uid,
        device_ip=device_ip,
        auth_token=device_auth_token(cfg, device_uid),
        stream_end=True,
    )


class ServerConfig:
    prefix: str
    store: SpeechStore
    device_registry: DeviceRegistry | None = None
    firmware_catalog: FirmwareCatalog | None = None
    echo_enabled: bool = False
    echo_device_ip: str = ""
    echo_auth_token: str = ""
    play_chunk_bytes: int = 24576
    tts_idle_flush_ms: int = 500
    tts_idle_poll_ms: int = 50
    tts_idle_flush_min_chars: int = _MIN_TTS_IDLE_FLUSH_CHARS
    tts_first_clause_chars: int = _MIN_TTS_FIRST_CLAUSE_CHARS
    tts_min_clause_chars: int = _MIN_TTS_CLAUSE_CHARS
    transcriber: WhisperTranscriber | None = None
    tts: ChatterboxTtsEngine | None = None
    hermes: HermesClient | None = None
    hermes_conversation_id: str = ""
    reply_ctx: dict[str, dict[str, str]]
    firmware_bin_path: str = ""
    firmware_version_override: str = ""
    public_base_url: str = ""


_FW_VERSION_BIN_RE = re.compile(r"^/firmware/([^/]+)/esp32-voice\.bin$")
_FW_VERSION_MANIFEST_RE = re.compile(r"^/firmware/([^/]+)/manifest\.json$")


class Handler(BaseHTTPRequestHandler):
    server: ThreadingHTTPServer  # type: ignore[assignment]

    @property
    def cfg(self) -> ServerConfig:
        return self.server.cfg  # type: ignore[attr-defined]

    def log_message(self, format: str, *args: Any) -> None:
        from bridge_log import blog_http

        blog_http(self.address_string(), format % args)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length > 0 else b"{}"
        data = json.loads(raw.decode("utf-8"))
        if not isinstance(data, dict):
            raise ValueError("JSON body must be an object")
        return data

    def _read_chunked_body(self) -> bytes:
        chunks: list[bytes] = []
        self._read_chunked_body_incremental(chunks.append)
        return b"".join(chunks)

    def _read_chunked_body_incremental(self, on_block) -> int:
        total = 0
        while True:
            line = self.rfile.readline()
            if not line:
                break
            size_line = line.strip().split(b";", 1)[0]
            if not size_line:
                continue
            chunk_size = int(size_line, 16)
            if chunk_size == 0:
                self.rfile.readline()
                break
            data = self.rfile.read(chunk_size)
            if data:
                on_block(data)
                total += len(data)
            self.rfile.readline()
        return total

    def _read_binary_body(self) -> bytes:
        transfer_encoding = self.headers.get("Transfer-Encoding", "").lower()
        if "chunked" in transfer_encoding:
            return self._read_chunked_body()

        length = self.headers.get("Content-Length")
        if length is not None:
            return self.rfile.read(int(length))
        return self.rfile.read()

    def _read_binary_body_incremental(self, on_block) -> int:
        transfer_encoding = self.headers.get("Transfer-Encoding", "").lower()
        if "chunked" in transfer_encoding:
            return self._read_chunked_body_incremental(on_block)

        length = self.headers.get("Content-Length")
        if length is None:
            data = self.rfile.read()
            if data:
                on_block(data)
            return len(data)

        remaining = int(length)
        total = 0
        while remaining > 0:
            block = self.rfile.read(min(8192, remaining))
            if not block:
                break
            on_block(block)
            total += len(block)
            remaining -= len(block)
        return total

    def _header(self, name: str, default: str = "") -> str:
        return self.headers.get(name, default)

    def _send_json(self, code: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _route_path(self) -> str:
        prefix = self.cfg.prefix.rstrip("/")
        path = urlparse(self.path).path
        if prefix and path.startswith(prefix):
            return path[len(prefix) :] or "/"
        return path

    def _send_bytes(self, code: int, body: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _public_base_url(self) -> str:
        if self.cfg.public_base_url:
            return self.cfg.public_base_url.rstrip("/")
        host = self.headers.get("Host", "").strip()
        if not host:
            return self.cfg.prefix.rstrip("/")
        return f"http://{host}{self.cfg.prefix.rstrip('/')}"

    def _resolve_firmware(self, version: str = "") -> FirmwareBundle | None:
        catalog = self.cfg.firmware_catalog
        if catalog is None:
            return None
        ver = version.strip()
        if ver:
            return catalog.bundle_for_version(ver)
        return catalog.active_bundle()

    def _serve_firmware_manifest(self, version: str = "") -> None:
        bundle = self._resolve_firmware(version)
        if bundle is None:
            self._send_json(
                404,
                {"v": 1, "error": {"code": "NOT_FOUND", "message": "Firmware binary not available"}},
            )
            return
        body = manifest_json(
            bundle=bundle,
            base_url=self._public_base_url(),
            version=version or bundle.version,
        )
        self._send_bytes(200, body, "application/json")

    def _serve_firmware_binary(self, version: str = "") -> None:
        bundle = self._resolve_firmware(version)
        if bundle is None:
            self._send_json(
                404,
                {"v": 1, "error": {"code": "NOT_FOUND", "message": "Firmware binary not available"}},
            )
            return
        data = bundle.bin_path.read_bytes()
        self._send_bytes(200, data, "application/octet-stream")

    def _try_ui_get(self, path: str) -> bool:
        ui: BridgeUiHandlers | None = getattr(self.server, "ui_handlers", None)
        if ui is None:
            return False
        result = ui.handle_get(path)
        if result is None:
            return False
        code, content_type, body = result
        self._send_bytes(code, body, content_type)
        return True

    def _try_ui_post(self, path: str) -> bool:
        ui: BridgeUiHandlers | None = getattr(self.server, "ui_handlers", None)
        if ui is None:
            return False

        if path == "/ui/firmware/upload":
            try:
                data, version_override = ui.parse_firmware_upload(self.headers, self.rfile)
                payload = ui.save_firmware_bytes(data, version_override=version_override)
                self._send_json(200, payload)
            except ValueError as exc:
                self._send_json(400, {"ok": False, "error": str(exc)})
            except OSError as exc:
                self._send_json(500, {"ok": False, "error": str(exc)})
            return True

        if path not in _UI_POST_PATHS:
            return False

        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length) if length > 0 else b""
        result = ui.handle_post(path, body, self.headers)
        if result is None:
            return False
        code, content_type, payload = result
        self._send_bytes(code, payload, content_type)
        return True

    def do_GET(self) -> None:
        path = self._route_path()

        if self._try_ui_get(path):
            return

        if path == "/firmware/manifest.json":
            self._serve_firmware_manifest()
            return

        if path == "/firmware/esp32-voice.bin":
            self._serve_firmware_binary()
            return

        manifest_match = _FW_VERSION_MANIFEST_RE.match(path)
        if manifest_match:
            self._serve_firmware_manifest(manifest_match.group(1))
            return

        bin_match = _FW_VERSION_BIN_RE.match(path)
        if bin_match:
            self._serve_firmware_binary(bin_match.group(1))
            return

        if path == "/status":
            self._send_json(200, {"v": 1, **self.cfg.store.status()})
            return

        self._send_json(404, {"v": 1, "error": {"code": "NOT_FOUND", "message": "Unknown route"}})

    def _handle_speech_stream(self) -> None:
        protocol = self._header("X-Protocol", "A3")
        if protocol not in ("A3", ""):
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": "Expected protocol A3"}})
            return

        utterance_id = self._header("X-Utterance-Id")
        if not utterance_id:
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": "Missing X-Utterance-Id"}})
            return

        try:
            sample_rate_hz = int(self._header("X-Sample-Rate", "16000"))
            channels = int(self._header("X-Channels", "1"))
        except ValueError as exc:
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": str(exc)}})
            return

        session_id = self._header("X-Session-Id")
        device_uid = self._header("X-Device-Uid")
        device_name = self._header("X-Device-Name")
        client_ip = self._header("X-Forwarded-For", "").split(",")[0].strip() or self.client_address[0]
        device_ip = self.cfg.echo_device_ip or client_ip
        self.cfg.store.note_device_ip(device_ip)
        if self.cfg.device_registry is not None:
            self.cfg.device_registry.note_seen(
                device_uid=device_uid,
                device_name=device_name,
                ip=device_ip,
            )

        self.cfg.store.begin_stream(
            utterance_id=utterance_id,
            session_id=session_id,
            device_uid=device_uid,
            device_name=device_name,
            sample_rate_hz=sample_rate_hz,
            channels=channels,
        )
        if self.cfg.transcriber is not None:
            self.cfg.transcriber.begin_utterance(
                utterance_id,
                sample_rate_hz=sample_rate_hz,
                channels=channels,
            )

        def on_pcm_block(block: bytes) -> None:
            self.cfg.store.append_stream_pcm(utterance_id, block)
            if self.cfg.transcriber is not None:
                self.cfg.transcriber.feed(
                    utterance_id,
                    block,
                    sample_rate_hz=sample_rate_hz,
                    channels=channels,
                )

        try:
            pcm_len = self._read_binary_body_incremental(on_pcm_block)
        except OSError as exc:
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": str(exc)}})
            return

        duration_s = pcm_len / (sample_rate_hz * channels * 2) if pcm_len else 0.0
        blog(
            f"[stream] {device_uid or 'unknown'} {utterance_id}: "
            f"{pcm_len} bytes ({sample_rate_hz} Hz, {channels} ch, ~{duration_s:.2f}s)",
        )
        self._send_json(
            200,
            {
                "v": 1,
                "ok": True,
                "protocol": "A3",
                "utterance_id": utterance_id,
                "pcm_bytes": pcm_len,
            },
        )

    def do_POST(self) -> None:
        path = self._route_path()

        if self._try_ui_post(path):
            return

        if path == "/speech/stream":
            self._handle_speech_stream()
            return

        try:
            payload = self._read_json()
        except (json.JSONDecodeError, ValueError) as exc:
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": str(exc)}})
            return

        if payload.get("protocol") not in (None, "A2", "A3"):
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": "Expected protocol A2 or A3"}})
            return

        if path == "/speech":
            try:
                self.cfg.store.add_chunk(payload)
            except (KeyError, ValueError, binascii.Error) as exc:
                self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": str(exc)}})
                return
            if self.cfg.device_registry is not None:
                client_ip = self.client_address[0]
                device_ip = self.cfg.echo_device_ip or client_ip
                self.cfg.device_registry.note_seen(
                    device_uid=str(payload.get("device_uid", "")),
                    device_name=str(payload.get("device_name", "")),
                    ip=device_ip,
                )
            self._send_json(
                200,
                {
                    "v": 1,
                    "ok": True,
                    "utterance_id": payload.get("utterance_id"),
                    "chunk_seq": payload.get("chunk_seq"),
                },
            )
            return

        if path == "/speech/finalize":
            result = self.cfg.store.finalize(payload)
            if not result.get("ok"):
                self._send_json(404, {"v": 1, "error": result})
                return

            utterance_id = str(result.get("utterance_id", ""))
            device_uid = str(result.get("device_uid", ""))
            client_ip = self.client_address[0]
            device_ip = self.cfg.echo_device_ip or client_ip
            if self.cfg.device_registry is not None and device_uid:
                device_ip = self.cfg.device_registry.resolve_ip(device_uid, device_ip)
                self.cfg.device_registry.note_seen(
                    device_uid=device_uid,
                    device_name=str(result.get("device_name", "")),
                    ip=device_ip,
                )
            if utterance_id and device_uid:
                self.cfg.reply_ctx[utterance_id] = {
                    "device_uid": device_uid,
                    "device_ip": device_ip,
                    "session_id": str(result.get("session_id", "")),
                    "finalize_ts": time.time(),
                }

            # ESP finalize waits only ~5s; reply before echo/ASR work.
            self._send_json(200, {"v": 1, **result})

            def post_finalize() -> None:
                if self.cfg.echo_enabled and device_uid:
                    pcm_path = Path(result["wav_path"])
                    with wave.open(str(pcm_path), "rb") as wf:
                        pcm = wf.readframes(wf.getnframes())
                        echo_playback(
                            device_ip,
                            result["device_uid"],
                            pcm,
                            int(result["sample_rate_hz"]),
                            int(result["channels"]),
                            device_auth_token(self.cfg, result["device_uid"]),
                            chunk_bytes=self.cfg.play_chunk_bytes,
                        )

                if self.cfg.transcriber is not None and utterance_id:
                    if not getattr(self.server, "streaming_asr", False):
                        with wave.open(str(result["wav_path"]), "rb") as wf:
                            pcm = wf.readframes(wf.getnframes())
                        self.cfg.transcriber.load_pcm_and_finalize(
                            utterance_id,
                            pcm,
                            sample_rate_hz=int(result["sample_rate_hz"]),
                            channels=int(result["channels"]),
                        )
                    else:
                        self.cfg.transcriber.finalize_utterance(utterance_id)

            threading.Thread(
                target=post_finalize,
                name=f"finalize-{utterance_id or 'unknown'}",
                daemon=True,
            ).start()
            return

        if path == "/tts/speak":
            if self.cfg.tts is None:
                self._send_json(
                    503,
                    {"v": 1, "error": {"code": "TTS_DISABLED", "message": "TTS is disabled"}},
                )
                return

            text = str(payload.get("text", "")).strip()
            device_uid = str(payload.get("device_uid", "")).strip()
            if not text or not device_uid:
                self._send_json(
                    400,
                    {"v": 1, "error": {"code": "INVALID_REQUEST", "message": "text and device_uid required"}},
                )
                return

            device_ip = str(payload.get("device_ip", "")).strip()
            if not device_ip and self.cfg.device_registry is not None:
                device_ip = self.cfg.device_registry.resolve_ip(device_uid, "")
            if not device_ip:
                device_ip = self.cfg.echo_device_ip or self.client_address[0]

            prompt_wav = payload.get("audio_prompt_path") or payload.get("prompt_wav_path")
            utterance_id = str(payload.get("utterance_id") or f"tts_{int(time.time())}")

            self.cfg.tts.speak_async(
                utterance_id,
                text,
                device_uid=device_uid,
                device_ip=device_ip,
                prompt_wav_path=str(prompt_wav) if prompt_wav else None,
                auth_token=device_auth_token(self.cfg, device_uid),
            )
            self._send_json(
                200,
                {
                    "v": 1,
                    "ok": True,
                    "utterance_id": utterance_id,
                    "device_uid": device_uid,
                    "device_ip": device_ip,
                    "queued": True,
                },
            )
            return

        self._send_json(404, {"v": 1, "error": {"code": "NOT_FOUND", "message": "Unknown route"}})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Hermes-Voice-Bridge backend server")
    parser.add_argument("--host", default="0.0.0.0", help="Listen address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8080, help="Listen port (default: 8080)")
    parser.add_argument("--prefix", default="/api/v1", help="URL prefix (default: /api/v1)")
    parser.add_argument(
        "--recordings-dir",
        default=None,
        help="Directory for WAV files (default: tools/hermes_voice_bridge/recordings)",
    )
    parser.add_argument(
        "--echo-on",
        action="store_true",
        help="After finalize, play audio back on the ESP (default: off)",
    )
    parser.add_argument(
        "--echo-device-ip",
        default="",
        help="Fixed ESP IP for echo /play (default: use upload client IP)",
    )
    parser.add_argument("--echo-auth-token", default="", help="X-Auth-Token for echo /play")
    parser.add_argument(
        "--play-chunk-bytes",
        type=int,
        default=24576,
        help="PCM bytes per /play POST (default: 24576, ~768 ms @ 16 kHz)",
    )
    parser.add_argument(
        "--tts-idle-flush-ms",
        type=int,
        default=500,
        help="Flush pending Hermes text to TTS after N ms without new tokens (default: 500, 0=off)",
    )
    parser.add_argument(
        "--tts-idle-poll-ms",
        type=int,
        default=50,
        help="Poll interval for Hermes idle flush watchdog (default: 50)",
    )
    parser.add_argument(
        "--tts-first-clause-chars",
        type=int,
        default=_MIN_TTS_FIRST_CLAUSE_CHARS,
        help="Min chars for first streamed TTS clause (default: 12)",
    )
    parser.add_argument(
        "--tts-play-lead-ms",
        type=int,
        default=1000,
        help="Buffer synthesized PCM before first ESP send (default: 1000, 0=off)",
    )
    parser.add_argument(
        "--tts-coalesce-ms",
        type=int,
        default=0,
        help="Merge consecutive TTS chunks within N ms after first chunk (default: 0=off)",
    )
    parser.add_argument(
        "--tts-idle-flush-min-chars",
        type=int,
        default=_MIN_TTS_IDLE_FLUSH_CHARS,
        help="Min chars before idle-flush sends pending Hermes text (default: 20)",
    )
    parser.add_argument(
        "--no-hermes-stream",
        action="store_true",
        help="Wait for full Hermes reply instead of streaming sentence TTS",
    )
    parser.add_argument(
        "--no-whisper",
        action="store_true",
        help="Disable faster-whisper transcription on finalize",
    )
    parser.add_argument(
        "--whisper-model",
        default="base",
        help="faster-whisper model size/name (default: base)",
    )
    parser.add_argument(
        "--whisper-device",
        default="cuda",
        choices=("cuda", "cpu", "auto"),
        help="Inference device (default: cuda)",
    )
    parser.add_argument(
        "--whisper-compute-type",
        default="float16",
        help="ctranslate2 compute type (default: float16; use int8 on CPU)",
    )
    parser.add_argument(
        "--whisper-beam-size",
        type=int,
        default=1,
        help="Beam size for decoding (default: 1)",
    )
    parser.add_argument(
        "--whisper-language",
        default="en",
        help="Whisper language code (default: en). Use 'auto' to detect language.",
    )
    parser.add_argument(
        "--whisper-flush-ms",
        type=int,
        default=1200,
        help="Run partial ASR every N ms of new audio during upload (default: 1200)",
    )
    parser.add_argument(
        "--streaming-asr",
        action="store_true",
        help="Run partial ASR during upload ([asr+] logs); default is finalize-only",
    )
    parser.add_argument(
        "--no-streaming-asr",
        action="store_false",
        dest="streaming_asr",
        help=argparse.SUPPRESS,
    )
    parser.set_defaults(streaming_asr=False)
    parser.add_argument(
        "--no-tts",
        action="store_true",
        help="Disable Chatterbox TTS replies to the ESP",
    )
    parser.add_argument(
        "--tts-model",
        default="turbo",
        choices=("english", "turbo", "multilingual"),
        help="Chatterbox model (default: turbo; paralinguistic tags need turbo)",
    )
    parser.add_argument(
        "--tts-device",
        default="cuda",
        choices=("cuda", "cpu", "mps"),
        help="Chatterbox inference device (default: cuda)",
    )
    parser.add_argument(
        "--tts-sample-rate-hz",
        type=int,
        default=16000,
        help="Resample TTS output for ESP playback (default: 16000)",
    )
    parser.add_argument(
        "--tts-voice-wav",
        default="",
        help="Override Chatterbox voice clone WAV (default: voices/default_female.wav)",
    )
    parser.add_argument(
        "--tts-attn",
        default="sdpa",
        choices=("sdpa", "eager"),
        help="Chatterbox attention backend (default: sdpa; use eager if generate() fails)",
    )
    parser.add_argument(
        "--tts-builtin-voice",
        action="store_true",
        help="Use Chatterbox builtin voice instead of the default female clone",
    )
    parser.add_argument(
        "--hf-token",
        default="",
        help="Hugging Face token (or set HF_TOKEN env var)",
    )
    parser.add_argument(
        "--no-hermes",
        action="store_true",
        help="Skip Hermes LLM; speak ASR transcript directly",
    )
    parser.add_argument(
        "--hermes-host",
        default="192.168.100.25",
        help="Hermes agent host (default: 192.168.100.25)",
    )
    parser.add_argument(
        "--hermes-port",
        type=int,
        default=8642,
        help="Hermes agent port (default: 8642)",
    )
    parser.add_argument(
        "--hermes-api-key",
        default="",
        help="Hermes bearer token (or set HERMES_API_KEY env var)",
    )
    parser.add_argument(
        "--hermes-model",
        default="hermes-agent",
        help="Hermes model name (default: hermes-agent)",
    )
    parser.add_argument(
        "--hermes-conversation-prefix",
        default="voice-box",
        help="Hermes conversation id prefix (default: voice-box -> voice-box-<device_uid>)",
    )
    parser.add_argument(
        "--hermes-voice-instructions",
        default="",
        help="Voice-mode instructions sent only on first message per conversation",
    )
    parser.add_argument(
        "--hermes-system-prompt",
        default="",
        help="Deprecated alias for --hermes-voice-instructions",
    )
    parser.add_argument(
        "--hermes-timeout-s",
        type=float,
        default=300.0,
        help="Hermes read timeout in seconds (default: 300)",
    )
    parser.add_argument(
        "--hermes-connect-timeout-s",
        type=float,
        default=10.0,
        help="Hermes connect timeout in seconds (default: 10)",
    )
    parser.add_argument(
        "--hermes-max-retries",
        type=int,
        default=1,
        help="Retry Hermes once on connection drops (default: 1)",
    )
    session_group = parser.add_mutually_exclusive_group()
    session_group.add_argument(
        "--new-session",
        action="store_true",
        help="Create a new Hermes conversation id, save it, and use it",
    )
    session_group.add_argument(
        "--session",
        metavar="SESSIONID",
        default=None,
        help="Restore a saved Hermes conversation id and make it active",
    )
    parser.add_argument(
        "--hermes-session-file",
        default="",
        help=f"Path to active Hermes session file (default: {DEFAULT_SESSION_FILE})",
    )
    parser.add_argument(
        "--debug",
        nargs="?",
        const="",
        default=None,
        metavar="FILE",
        help="Pipeline log with unix-ms timestamps to stdout and FILE "
        "(default: <repo>/local/bridge-debug.log)",
    )
    parser.add_argument(
        "--firmware-bin",
        default="",
        help="Path to esp32-voice.bin for OTA hosting (default: dist or firmware/build)",
    )
    parser.add_argument(
        "--firmware-version",
        default="",
        help="Override firmware version in OTA manifest (default: read from deployed binary)",
    )
    parser.add_argument(
        "--public-base-url",
        default="",
        help="Public base URL for OTA manifest links (default: http://<Host>/<prefix>)",
    )
    parser.add_argument(
        "--device-ip",
        default="",
        help="Default ESP32 device IP for the web admin UI",
    )
    parser.add_argument(
        "--ota-secret",
        default="",
        help="Default OTA secret for the web admin UI",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    here = Path(__file__).resolve().parent
    if args.debug is not None:
        default_log = here.parent.parent / "local" / "bridge-debug.log"
        debug_path = args.debug.strip() or str(default_log)
        configure_bridge_log(debug_path)
    configure_ml_runtime(hf_token=args.hf_token)
    recordings = Path(args.recordings_dir) if args.recordings_dir else here / "recordings"

    cfg = ServerConfig()
    cfg.prefix = args.prefix if args.prefix.startswith("/") else f"/{args.prefix}"
    cfg.store = SpeechStore(recordings)
    cfg.echo_enabled = args.echo_on
    cfg.echo_device_ip = args.echo_device_ip
    cfg.echo_auth_token = args.echo_auth_token
    cfg.play_chunk_bytes = max(2, args.play_chunk_bytes - (args.play_chunk_bytes % 2))
    cfg.tts_idle_flush_ms = max(0, args.tts_idle_flush_ms)
    cfg.tts_idle_poll_ms = max(20, args.tts_idle_poll_ms)
    cfg.tts_idle_flush_min_chars = max(3, args.tts_idle_flush_min_chars)
    cfg.tts_first_clause_chars = max(3, args.tts_first_clause_chars)
    cfg.tts_min_clause_chars = _MIN_TTS_CLAUSE_CHARS
    cfg.reply_ctx = {}
    cfg.firmware_bin_path = args.firmware_bin.strip()
    cfg.firmware_version_override = args.firmware_version.strip()
    cfg.public_base_url = args.public_base_url.strip()

    firmware_catalog = FirmwareCatalog(here / "uploads" / "firmware")
    firmware_catalog.import_dist_default()
    if cfg.firmware_bin_path:
        firmware_catalog.import_file(
            Path(cfg.firmware_bin_path),
            version_override=cfg.firmware_version_override,
            source="cli",
        )
    cfg.firmware_catalog = firmware_catalog
    firmware_bundle = firmware_catalog.active_bundle()

    def tts_playback(
        device_ip: str,
        device_uid: str,
        pcm: bytes,
        sample_rate_hz: int,
        channels: int,
        auth_token: str,
        *,
        stream_end: bool = True,
    ) -> bool:
        return stream_pcm_to_esp(
            device_ip,
            device_uid,
            pcm,
            sample_rate_hz,
            channels,
            auth_token,
            chunk_bytes=cfg.play_chunk_bytes,
            stream_end=stream_end,
            log_prefix="[tts]",
        )

    cfg.tts = build_tts_from_args(args, playback_fn=tts_playback)
    cfg.hermes = build_hermes_from_args(args)

    hermes_session_file = Path(args.hermes_session_file) if args.hermes_session_file else DEFAULT_SESSION_FILE
    hermes_session_mode = ""
    if cfg.hermes is not None:
        try:
            cfg.hermes_conversation_id, hermes_session_mode = resolve_active_session(
                path=hermes_session_file,
                prefix=args.hermes_conversation_prefix,
                new_session=args.new_session,
                session_id=args.session,
            )
        except ValueError as exc:
            blog(f"Hermes session error: {exc}")
            return 2
        if hermes_session_mode in {"restored", "continued"}:
            cfg.hermes.mark_conversation_introduced(cfg.hermes_conversation_id)

    def on_asr_final(utterance_id: str, text: str) -> None:
        ctx = cfg.reply_ctx.pop(utterance_id, None)
        if ctx is None:
            return

        finalize_ts = ctx.get("finalize_ts")
        if isinstance(finalize_ts, (int, float)):
            blog(
                f"[latency] {utterance_id}: finalize -> asr_final {time.time() - finalize_ts:.2f}s",
            )

        def run() -> None:
            t0 = time.time()
            handle_voice_reply(
                cfg,
                utterance_id,
                text,
                device_uid=ctx["device_uid"],
                device_ip=ctx["device_ip"],
                session_id=ctx.get("session_id", ""),
            )
            blog(
                f"[latency] {utterance_id}: reply pipeline {time.time() - t0:.2f}s",
            )

        threading.Thread(target=run, name=f"reply-{utterance_id}", daemon=True).start()

    cfg.transcriber = build_transcriber_from_args(args, on_final=on_asr_final)

    if cfg.tts is not None:
        cfg.tts.preload()
        cfg.tts.start()

    if cfg.transcriber is not None:
        cfg.transcriber.preload()
        cfg.transcriber.start()

    settings_store = BridgeSettingsStore(here / "bridge_settings.json")
    settings_store.apply_defaults(**settings_defaults_from_env())
    settings_store.apply_defaults(
        device_ip=args.device_ip or args.echo_device_ip or "",
        ota_secret=args.ota_secret or "",
        auth_token=args.echo_auth_token or "",
    )

    device_registry = DeviceRegistry(here / "devices.json")
    env_defaults = settings_defaults_from_env()
    device_registry.apply_global_defaults(
        ota_secret=args.ota_secret or env_defaults.get("ota_secret", ""),
        auth_token=args.echo_auth_token or env_defaults.get("auth_token", ""),
    )
    device_registry.migrate_legacy_settings(settings_store.settings)
    seed_ip = args.device_ip or args.echo_device_ip or settings_store.settings.device_ip
    if seed_ip and not device_registry.list_devices():
        device_registry.seed_manual_device(
            ip=seed_ip,
            ota_secret=settings_store.settings.ota_secret,
            auth_token=settings_store.settings.auth_token,
        )
    cfg.device_registry = device_registry
    device_registry.ensure_all_auth_tokens()

    def public_base_url() -> str:
        if cfg.public_base_url:
            return cfg.public_base_url.rstrip("/")
        return f"http://{args.host}:{args.port}{cfg.prefix.rstrip('/')}"

    ui_handlers = BridgeUiHandlers(
        device_registry,
        firmware_catalog=firmware_catalog,
        uploads_dir=here / "uploads",
        public_base_url_resolver=public_base_url,
        last_device_ip_getter=lambda: cfg.store.last_device_ip,
        echo_auth_token_getter=lambda: cfg.echo_auth_token,
    )

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.cfg = cfg  # type: ignore[attr-defined]
    server.ui_handlers = ui_handlers  # type: ignore[attr-defined]
    server.streaming_asr = args.streaming_asr  # type: ignore[attr-defined]

    blog("Hermes-Voice-Bridge")
    blog(f"Admin UI -> http://{args.host}:{args.port}{cfg.prefix}/ui")
    blog(f"Listening on http://{args.host}:{args.port}{cfg.prefix}")
    blog("Protocol A3 (primary):")
    blog(f"  POST {cfg.prefix}/speech/stream  (binary PCM, chunked HTTP body + X-* headers)")
    blog(f"  POST {cfg.prefix}/speech/finalize  (JSON metadata after stream closes)")
    blog(f"  POST {cfg.prefix}/tts/speak  (JSON text -> Chatterbox -> ESP /play)")
    blog("Legacy A2:")
    blog(f"  POST {cfg.prefix}/speech  (JSON + pcm_b64 per chunk)")
    blog(f"  GET  {cfg.prefix}/status")
    blog(f"Recordings -> {recordings}")
    if args.echo_on:
        target = args.echo_device_ip or "<upload client IP>"
        blog(f"Echo playback -> http://{target}/api/v1/play")
    else:
        blog("Echo -> disabled")
    if cfg.transcriber is not None:
        mode = "streaming" if args.streaming_asr else "finalize-only"
        blog(
            f"ASR -> faster-whisper model={args.whisper_model} "
            f"device={args.whisper_device} compute={args.whisper_compute_type} "
            f"language={cfg.transcriber.language_label} "
            f"mode={mode} flush_ms={args.whisper_flush_ms}",
        )
    else:
        blog("ASR -> disabled")
    if cfg.hermes is not None:
        blog(
            f"Hermes -> {cfg.hermes.endpoint} model={cfg.hermes.model} "
            f"conversation={cfg.hermes_conversation_id} ({hermes_session_mode}) "
            f"session_file={hermes_session_file} "
            f"read_timeout={cfg.hermes.read_timeout_s:.0f}s retries={cfg.hermes._max_retries}",
        )
    else:
        blog("Hermes -> disabled")
    if cfg.tts is not None:
        blog(
            f"TTS -> chatterbox model={args.tts_model} device={args.tts_device} "
            f"attn={args.tts_attn} out_rate={args.tts_sample_rate_hz}Hz voice={cfg.tts.voice_label} "
            f"idle_flush={cfg.tts_idle_flush_ms}ms lead={args.tts_play_lead_ms}ms "
            f"coalesce={args.tts_coalesce_ms}ms play_chunk={cfg.play_chunk_bytes}B",
        )
    else:
        blog("TTS -> disabled")
    if firmware_bundle is not None:
        active_ver = firmware_catalog.active_version()
        count = len(firmware_catalog.list_entries())
        blog(
            f"OTA host -> {cfg.prefix}/firmware/manifest.json "
            f"active={active_ver} ({count} firmware(s) in catalog) "
            f"bin={firmware_bundle.bin_path}",
        )
    else:
        blog("OTA host -> no firmware binary (run tools/publish_firmware.sh after build)")
    blog("")
    blog("On the BOX serial CLI (use your PC/LAN IP):")
    blog(f"  callback_base_set http://YOUR_LAN_IP:{args.port}{cfg.prefix}")
    blog("  config_save")
    blog("")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        blog("\nStopped.")
    finally:
        if cfg.transcriber is not None:
            cfg.transcriber.stop()
        if cfg.tts is not None:
            cfg.tts.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
