#!/usr/bin/env python3
"""
Minimal REST server for ESP32-Voice speech upload testing.

Endpoints (under --prefix, default /api/v1):
  POST /speech/stream    - receive streamed PCM (A3 binary chunked HTTP)
  POST /speech           - receive PCM chunks (legacy A2 JSON + pcm_b64)
  POST /speech/finalize  - finalize utterance, save WAV, optional echo playback
  GET  /status           - recent utterances and saved recordings
"""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import sys
import threading
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
        self.recordings_dir = recordings_dir
        self.recordings_dir.mkdir(parents=True, exist_ok=True)

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

        print(
            f"[speech] {utt.device_uid} {utterance_id} ({utt.protocol}): "
            f"{chunks_received} segments, {len(pcm)} bytes -> {wav_path}",
            flush=True,
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
) -> bool:
    if requests is None:
        print("[echo] requests not installed; skip playback", flush=True)
        return False

    url = f"http://{device_ip}/api/v1/play"
    body = {
        "v": 1,
        "target_device_uid": device_uid,
        "request_id": "test_server_echo",
        "command_id": "echo_1",
        "sample_rate_hz": sample_rate_hz,
        "channels": channels,
        "pcm_b64": base64.b64encode(pcm).decode("ascii"),
    }
    headers = {"Content-Type": "application/json"}
    if auth_token:
        headers["X-Auth-Token"] = auth_token

    try:
        resp = requests.post(url, json=body, headers=headers, timeout=10)
        ok = resp.status_code == 200
        print(f"[echo] POST /play -> {resp.status_code}", flush=True)
        return ok
    except requests.RequestException as exc:
        print(f"[echo] failed: {exc}", flush=True)
        return False


class ServerConfig:
    prefix: str
    store: SpeechStore
    echo_device_ip: str
    echo_auth_token: str


class Handler(BaseHTTPRequestHandler):
    server: ThreadingHTTPServer  # type: ignore[assignment]

    @property
    def cfg(self) -> ServerConfig:
        return self.server.cfg  # type: ignore[attr-defined]

    def log_message(self, format: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), format % args))

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length > 0 else b"{}"
        data = json.loads(raw.decode("utf-8"))
        if not isinstance(data, dict):
            raise ValueError("JSON body must be an object")
        return data

    def _read_chunked_body(self) -> bytes:
        chunks: list[bytes] = []
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
            chunks.append(self.rfile.read(chunk_size))
            self.rfile.readline()
        return b"".join(chunks)

    def _read_binary_body(self) -> bytes:
        transfer_encoding = self.headers.get("Transfer-Encoding", "").lower()
        if "chunked" in transfer_encoding:
            return self._read_chunked_body()

        length = self.headers.get("Content-Length")
        if length is not None:
            return self.rfile.read(int(length))
        return self.rfile.read()

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

    def do_GET(self) -> None:
        if self._route_path() == "/status":
            self._send_json(200, {"v": 1, **self.cfg.store.status()})
            return

        if self._route_path() == "/":
            status = self.cfg.store.status()
            lines = [
                "ESP32-Voice test server",
                "",
                f"POST {self.cfg.prefix}/speech/stream",
                f"POST {self.cfg.prefix}/speech",
                f"POST {self.cfg.prefix}/speech/finalize",
                f"GET  {self.cfg.prefix}/status",
                "",
                f"Recordings: {status['recordings_dir']}",
                f"Active utterances: {len(status['active_utterances'])}",
            ]
            body = "\n".join(lines).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
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
            pcm = self._read_binary_body()
            sample_rate_hz = int(self._header("X-Sample-Rate", "16000"))
            channels = int(self._header("X-Channels", "1"))
        except (ValueError, OSError) as exc:
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": str(exc)}})
            return

        self.cfg.store.add_stream(
            utterance_id=utterance_id,
            session_id=self._header("X-Session-Id"),
            device_uid=self._header("X-Device-Uid"),
            device_name=self._header("X-Device-Name"),
            pcm=pcm,
            sample_rate_hz=sample_rate_hz,
            channels=channels,
        )
        device_uid = self._header("X-Device-Uid", "unknown")
        duration_s = len(pcm) / (sample_rate_hz * channels * 2) if pcm else 0.0
        print(
            f"[stream] {device_uid} {utterance_id}: "
            f"{len(pcm)} bytes ({sample_rate_hz} Hz, {channels} ch, ~{duration_s:.2f}s)",
            flush=True,
        )
        self._send_json(
            200,
            {
                "v": 1,
                "ok": True,
                "protocol": "A3",
                "utterance_id": utterance_id,
                "pcm_bytes": len(pcm),
            },
        )

    def do_POST(self) -> None:
        path = self._route_path()

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

            if self.cfg.echo_device_ip and result.get("device_uid"):
                pcm_path = Path(result["wav_path"])
                with wave.open(str(pcm_path), "rb") as wf:
                    pcm = wf.readframes(wf.getnframes())
                    echo_playback(
                        self.cfg.echo_device_ip,
                        result["device_uid"],
                        pcm,
                        int(result["sample_rate_hz"]),
                        int(result["channels"]),
                        self.cfg.echo_auth_token,
                    )

            self._send_json(200, {"v": 1, **result})
            return

        self._send_json(404, {"v": 1, "error": {"code": "NOT_FOUND", "message": "Unknown route"}})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ESP32-Voice speech test server")
    parser.add_argument("--host", default="0.0.0.0", help="Listen address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8080, help="Listen port (default: 8080)")
    parser.add_argument("--prefix", default="/api/v1", help="URL prefix (default: /api/v1)")
    parser.add_argument(
        "--recordings-dir",
        default=None,
        help="Directory for WAV files (default: tools/test_server/recordings)",
    )
    parser.add_argument(
        "--echo-device-ip",
        default="",
        help="After finalize, POST recorded audio to http://IP/api/v1/play (optional)",
    )
    parser.add_argument("--echo-auth-token", default="", help="X-Auth-Token for echo /play")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    here = Path(__file__).resolve().parent
    recordings = Path(args.recordings_dir) if args.recordings_dir else here / "recordings"

    cfg = ServerConfig()
    cfg.prefix = args.prefix if args.prefix.startswith("/") else f"/{args.prefix}"
    cfg.store = SpeechStore(recordings)
    cfg.echo_device_ip = args.echo_device_ip
    cfg.echo_auth_token = args.echo_auth_token

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.cfg = cfg  # type: ignore[attr-defined]

    print(f"Listening on http://{args.host}:{args.port}{cfg.prefix}", flush=True)
    print("Protocol A3 (primary):", flush=True)
    print(f"  POST {cfg.prefix}/speech/stream  (binary PCM, chunked HTTP body + X-* headers)", flush=True)
    print(f"  POST {cfg.prefix}/speech/finalize  (JSON metadata after stream closes)", flush=True)
    print("Legacy A2:", flush=True)
    print(f"  POST {cfg.prefix}/speech  (JSON + pcm_b64 per chunk)", flush=True)
    print(f"  GET  {cfg.prefix}/status", flush=True)
    print(f"Recordings -> {recordings}", flush=True)
    if args.echo_device_ip:
        print(f"Echo playback -> http://{args.echo_device_ip}/api/v1/play", flush=True)
    print("", flush=True)
    print("On the BOX serial CLI (use your PC/LAN IP):", flush=True)
    print(f"  callback_base_set http://YOUR_LAN_IP:{args.port}{cfg.prefix}", flush=True)
    print("  config_save", flush=True)
    print("", flush=True)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
