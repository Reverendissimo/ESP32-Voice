#!/usr/bin/env python3
"""
Minimal REST server for ESP32-Voice speech upload testing.

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

from esp_playback import stream_pcm_to_esp
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
    *,
    chunk_bytes: int = 8192,
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


def hermes_failure_reply(kind: str) -> str:
    if kind == "timeout":
        return "Sorry, the assistant is taking too long. Please try again."
    if kind == "http":
        return "Sorry, the assistant had a problem. Please try again."
    return "Sorry, I could not reach the assistant right now."


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
        pending = ""
        tts_started = False

        def on_delta(delta: str) -> None:
            nonlocal pending, tts_started
            pending += delta
            sentences, pending = _split_pending_sentences(pending)
            for sentence in sentences:
                print(f"[hermes+] {utterance_id}: {sentence}", flush=True)
                if cfg.tts is not None:
                    tts_started = True
                    cfg.tts.speak_async(
                        utterance_id,
                        sentence,
                        device_uid=device_uid,
                        device_ip=device_ip,
                        auth_token=cfg.echo_auth_token,
                        stream_end=False,
                    )

        try:
            reply = cfg.hermes.chat(
                user_text,
                conversation_id=cfg.hermes_conversation_id,
                on_delta=on_delta,
            )
            if reply:
                print(f"[hermes] {utterance_id}: {reply}", flush=True)
            else:
                print(f"[hermes] {utterance_id}: (empty reply)", flush=True)
                reply = user_text
        except HermesError as exc:
            print(f"[hermes] {utterance_id}: {exc.kind}: {exc}", flush=True)
            reply = hermes_failure_reply(exc.kind)
        except Exception as exc:
            print(f"[hermes] {utterance_id}: unexpected error: {exc}", flush=True)
            traceback.print_exc()
            reply = hermes_failure_reply("unknown")
        else:
            remainder = pending.strip()
            if remainder and cfg.tts is not None:
                tts_started = True
                cfg.tts.speak_async(
                    utterance_id,
                    remainder,
                    device_uid=device_uid,
                    device_ip=device_ip,
                    auth_token=cfg.echo_auth_token,
                    stream_end=True,
                )
            elif tts_started and cfg.tts is not None:
                stream_pcm_to_esp(
                    device_ip,
                    device_uid,
                    b"",
                    16000,
                    1,
                    cfg.echo_auth_token,
                    stream_end=True,
                    log_prefix="[tts]",
                )
            return

    if cfg.tts is None or not reply:
        return

    cfg.tts.speak_async(
        utterance_id,
        reply,
        device_uid=device_uid,
        device_ip=device_ip,
        auth_token=cfg.echo_auth_token,
        stream_end=True,
    )


class ServerConfig:
    prefix: str
    store: SpeechStore
    echo_enabled: bool = False
    echo_device_ip: str = ""
    echo_auth_token: str = ""
    play_chunk_bytes: int = 8192
    transcriber: WhisperTranscriber | None = None
    tts: ChatterboxTtsEngine | None = None
    hermes: HermesClient | None = None
    hermes_conversation_id: str = ""
    reply_ctx: dict[str, dict[str, str]]


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
                f"POST {self.cfg.prefix}/tts/speak",
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
            sample_rate_hz = int(self._header("X-Sample-Rate", "16000"))
            channels = int(self._header("X-Channels", "1"))
        except ValueError as exc:
            self._send_json(400, {"v": 1, "error": {"code": "INVALID_REQUEST", "message": str(exc)}})
            return

        session_id = self._header("X-Session-Id")
        device_uid = self._header("X-Device-Uid")
        device_name = self._header("X-Device-Name")

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
        print(
            f"[stream] {device_uid or 'unknown'} {utterance_id}: "
            f"{pcm_len} bytes ({sample_rate_hz} Hz, {channels} ch, ~{duration_s:.2f}s)",
            flush=True,
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

            utterance_id = str(result.get("utterance_id", ""))
            device_uid = str(result.get("device_uid", ""))
            if utterance_id and device_uid:
                self.cfg.reply_ctx[utterance_id] = {
                    "device_uid": device_uid,
                    "device_ip": self.cfg.echo_device_ip or self.client_address[0],
                    "session_id": str(result.get("session_id", "")),
                }

            if self.cfg.echo_enabled and device_uid:
                device_ip = self.cfg.echo_device_ip or self.client_address[0]
                pcm_path = Path(result["wav_path"])
                with wave.open(str(pcm_path), "rb") as wf:
                    pcm = wf.readframes(wf.getnframes())
                    echo_playback(
                        device_ip,
                        result["device_uid"],
                        pcm,
                        int(result["sample_rate_hz"]),
                        int(result["channels"]),
                        self.cfg.echo_auth_token,
                        chunk_bytes=self.cfg.play_chunk_bytes,
                    )

            if self.cfg.transcriber is not None and result.get("utterance_id"):
                utterance_id = str(result["utterance_id"])
                if getattr(self.server, "no_streaming_asr", False):
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

            self._send_json(200, {"v": 1, **result})
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
                auth_token=self.cfg.echo_auth_token,
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
        default=8192,
        help="PCM bytes per /play POST (default: 8192, ~256 ms @ 16 kHz)",
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
        "--no-streaming-asr",
        action="store_true",
        help="Only transcribe on finalize (no incremental [asr+] during upload)",
    )
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
        help="Override Chatterbox voice clone WAV (default: voices/karla.wav)",
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
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    configure_ml_runtime(hf_token=args.hf_token)
    here = Path(__file__).resolve().parent
    recordings = Path(args.recordings_dir) if args.recordings_dir else here / "recordings"

    cfg = ServerConfig()
    cfg.prefix = args.prefix if args.prefix.startswith("/") else f"/{args.prefix}"
    cfg.store = SpeechStore(recordings)
    cfg.echo_enabled = args.echo_on
    cfg.echo_device_ip = args.echo_device_ip
    cfg.echo_auth_token = args.echo_auth_token
    cfg.play_chunk_bytes = max(2, args.play_chunk_bytes - (args.play_chunk_bytes % 2))
    cfg.reply_ctx = {}

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
            print(f"Hermes session error: {exc}", flush=True)
            return 2
        if hermes_session_mode in {"restored", "continued"}:
            cfg.hermes.mark_conversation_introduced(cfg.hermes_conversation_id)

    def on_asr_final(utterance_id: str, text: str) -> None:
        ctx = cfg.reply_ctx.pop(utterance_id, None)
        if ctx is None:
            return

        def run() -> None:
            handle_voice_reply(
                cfg,
                utterance_id,
                text,
                device_uid=ctx["device_uid"],
                device_ip=ctx["device_ip"],
                session_id=ctx.get("session_id", ""),
            )

        threading.Thread(target=run, name=f"reply-{utterance_id}", daemon=True).start()

    cfg.transcriber = build_transcriber_from_args(args, on_final=on_asr_final)

    if cfg.tts is not None:
        cfg.tts.preload()
        cfg.tts.start()

    if cfg.transcriber is not None:
        cfg.transcriber.preload()
        cfg.transcriber.start()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.cfg = cfg  # type: ignore[attr-defined]
    server.no_streaming_asr = args.no_streaming_asr  # type: ignore[attr-defined]

    print(f"Listening on http://{args.host}:{args.port}{cfg.prefix}", flush=True)
    print("Protocol A3 (primary):", flush=True)
    print(f"  POST {cfg.prefix}/speech/stream  (binary PCM, chunked HTTP body + X-* headers)", flush=True)
    print(f"  POST {cfg.prefix}/speech/finalize  (JSON metadata after stream closes)", flush=True)
    print(f"  POST {cfg.prefix}/tts/speak  (JSON text -> Chatterbox -> ESP /play)", flush=True)
    print("Legacy A2:", flush=True)
    print(f"  POST {cfg.prefix}/speech  (JSON + pcm_b64 per chunk)", flush=True)
    print(f"  GET  {cfg.prefix}/status", flush=True)
    print(f"Recordings -> {recordings}", flush=True)
    if args.echo_on:
        target = args.echo_device_ip or "<upload client IP>"
        print(f"Echo playback -> http://{target}/api/v1/play", flush=True)
    else:
        print("Echo -> disabled", flush=True)
    if cfg.transcriber is not None:
        mode = "streaming" if not args.no_streaming_asr else "finalize-only"
        print(
            f"ASR -> faster-whisper model={args.whisper_model} "
            f"device={args.whisper_device} compute={args.whisper_compute_type} "
            f"language={cfg.transcriber.language_label} "
            f"mode={mode} flush_ms={args.whisper_flush_ms}",
            flush=True,
        )
    else:
        print("ASR -> disabled", flush=True)
    if cfg.hermes is not None:
        print(
            f"Hermes -> {cfg.hermes.endpoint} model={cfg.hermes.model} "
            f"conversation={cfg.hermes_conversation_id} ({hermes_session_mode}) "
            f"session_file={hermes_session_file} "
            f"read_timeout={cfg.hermes.read_timeout_s:.0f}s retries={cfg.hermes._max_retries}",
            flush=True,
        )
    else:
        print("Hermes -> disabled", flush=True)
    if cfg.tts is not None:
        print(
            f"TTS -> chatterbox model={args.tts_model} device={args.tts_device} "
            f"out_rate={args.tts_sample_rate_hz}Hz voice={cfg.tts.voice_label}",
            flush=True,
        )
    else:
        print("TTS -> disabled", flush=True)
    print("", flush=True)
    print("On the BOX serial CLI (use your PC/LAN IP):", flush=True)
    print(f"  callback_base_set http://YOUR_LAN_IP:{args.port}{cfg.prefix}", flush=True)
    print("  config_save", flush=True)
    print("", flush=True)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.", flush=True)
    finally:
        if cfg.transcriber is not None:
            cfg.transcriber.stop()
        if cfg.tts is not None:
            cfg.tts.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
