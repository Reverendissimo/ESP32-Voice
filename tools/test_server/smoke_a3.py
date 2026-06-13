#!/usr/bin/env python3
"""
Smoke-test protocol A3 against a running test server.

Simulates the ESP32 flow:
  1. POST /speech/stream  — chunked binary PCM + metadata headers
  2. POST /speech/finalize — JSON metadata

Usage:
  ./tools/test_server/run.sh &
  python3 tools/test_server/smoke_a3.py --host 127.0.0.1 --port 8080
"""

from __future__ import annotations

import argparse
import http.client
import json
import sys
import wave
from pathlib import Path


def chunked_post_stream(
    host: str,
    port: int,
    prefix: str,
    headers: dict[str, str],
    pcm: bytes,
) -> tuple[int, dict]:
    path = f"{prefix.rstrip('/')}/speech/stream"
    conn = http.client.HTTPConnection(host, port, timeout=10)
    conn.putrequest("POST", path)
    conn.putheader("Content-Type", "application/octet-stream")
    conn.putheader("Transfer-Encoding", "chunked")
    for key, value in headers.items():
        conn.putheader(key, value)
    conn.endheaders()

    offset = 0
    chunk_size = 5120
    while offset < len(pcm):
        block = pcm[offset : offset + chunk_size]
        offset += len(block)
        conn.send(f"{len(block):x}\r\n".encode("ascii"))
        conn.send(block)
        conn.send(b"\r\n")
    conn.send(b"0\r\n\r\n")

    response = conn.getresponse()
    body = response.read().decode("utf-8")
    conn.close()
    try:
        payload = json.loads(body) if body else {}
    except json.JSONDecodeError:
        payload = {"raw": body}
    return response.status, payload


def post_finalize(host: str, port: int, prefix: str, payload: dict) -> tuple[int, dict]:
    path = f"{prefix.rstrip('/')}/speech/finalize"
    body = json.dumps(payload).encode("utf-8")
    conn = http.client.HTTPConnection(host, port, timeout=10)
    conn.request(
        "POST",
        path,
        body=body,
        headers={"Content-Type": "application/json", "Content-Length": str(len(body))},
    )
    response = conn.getresponse()
    raw = response.read().decode("utf-8")
    conn.close()
    try:
        parsed = json.loads(raw) if raw else {}
    except json.JSONDecodeError:
        parsed = {"raw": raw}
    return response.status, parsed


def make_test_pcm(seconds: float, sample_rate: int, channels: int) -> bytes:
    frames = int(sample_rate * seconds)
    # Quiet sine-ish pattern so the WAV is not literally silent zeros.
    samples = []
    for i in range(frames):
        value = int(1200 * ((i % 40) - 20))
        for _ in range(channels):
            samples.append(value)
    return b"".join(sample.to_bytes(2, "little", signed=True) for sample in samples)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Smoke-test A3 speech stream against test server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--prefix", default="/api/v1")
    parser.add_argument("--seconds", type=float, default=0.4, help="Test PCM duration")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sample_rate = 16000
    channels = 1
    pcm = make_test_pcm(args.seconds, sample_rate, channels)
    utterance_id = "smoke_a3_1"
    session_id = "smoke_sess"

    stream_headers = {
        "X-Protocol": "A3",
        "X-Device-Uid": "espbox-smoke",
        "X-Device-Name": "smoke-test",
        "X-Utterance-Id": utterance_id,
        "X-Session-Id": session_id,
        "X-Sample-Rate": str(sample_rate),
        "X-Channels": str(channels),
        "X-Sample-Format": "s16le",
    }

    print(f"POST {args.prefix}/speech/stream ({len(pcm)} bytes, chunked)...", flush=True)
    stream_status, stream_body = chunked_post_stream(
        args.host, args.port, args.prefix, stream_headers, pcm
    )
    print(f"  -> {stream_status} {stream_body}", flush=True)
    if stream_status != 200 or not stream_body.get("ok"):
        return 1

    finalize_payload = {
        "v": 1,
        "protocol": "A3",
        "device_uid": "espbox-smoke",
        "device_name": "smoke-test",
        "utterance_id": utterance_id,
        "session_id": session_id,
        "chunk_count": 2,
        "duration_ms": int(args.seconds * 1000),
    }
    print(f"POST {args.prefix}/speech/finalize ...", flush=True)
    fin_status, fin_body = post_finalize(args.host, args.port, args.prefix, finalize_payload)
    print(f"  -> {fin_status} {fin_body}", flush=True)
    if fin_status != 200 or not fin_body.get("ok"):
        return 1

    wav_path = Path(fin_body["wav_path"])
    if not wav_path.is_file():
        print(f"Missing WAV: {wav_path}", flush=True)
        return 1

    with wave.open(str(wav_path), "rb") as wf:
        if wf.getnchannels() != channels or wf.getframerate() != sample_rate:
            print("WAV metadata mismatch", flush=True)
            return 1
        if wf.getnframes() * wf.getsampwidth() * wf.getnchannels() != len(pcm):
            print("WAV size mismatch", flush=True)
            return 1

    print(f"A3 smoke OK -> {wav_path}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
