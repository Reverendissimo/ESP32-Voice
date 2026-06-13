#!/usr/bin/env python3
"""Stream a WAV file to ESP32 /api/v1/play in paced chunks."""

from __future__ import annotations

import argparse
import base64
import json
import sys
import time
import wave
from pathlib import Path

try:
    import requests
except ImportError:
    print("pip install requests", file=sys.stderr)
    raise


def play_wav(
    *,
    esp_url: str,
    device_uid: str,
    wav_path: Path,
    chunk_bytes: int = 10 * 1024,
    auth_token: str = "",
) -> int:
    with wave.open(str(wav_path), "rb") as w:
        pcm = w.readframes(w.getnframes())
        rate = w.getframerate()
        channels = w.getnchannels()

    chunk_bytes = chunk_bytes - (chunk_bytes % 2)
    bytes_per_sec = rate * channels * 2

    url = esp_url.rstrip("/") + "/api/v1/play"
    headers = {"Content-Type": "application/json"}
    if auth_token:
        headers["X-Auth-Token"] = auth_token

    total = len(pcm)
    sent = 0
    idx = 0
    print(f"Playing {wav_path.name}: {total / bytes_per_sec:.2f}s, {total} bytes, chunk={chunk_bytes}")

    while sent < total:
        piece = pcm[sent : sent + chunk_bytes]
        body = {
            "v": 1,
            "target_device_uid": device_uid,
            "request_id": f"play_{idx}",
            "command_id": f"chunk_{idx}",
            "sample_rate_hz": rate,
            "channels": channels,
            "pcm_b64": base64.b64encode(piece).decode("ascii"),
        }

        posted = False
        for attempt in range(8):
            resp = requests.post(url, json=body, headers=headers, timeout=30)
            if resp.status_code == 200:
                posted = True
                print(f"  chunk {idx}: {len(piece)} bytes ({sent + len(piece)}/{total})")
                break
            if resp.status_code == 503:
                time.sleep(0.2)
                continue
            print(f"  chunk {idx}: HTTP {resp.status_code} {resp.text[:120]}", file=sys.stderr)
            return 1

        if not posted:
            print(f"  chunk {idx}: queue full after retries", file=sys.stderr)
            return 1

        sent += len(piece)
        idx += 1

    print(f"Done — {idx} chunks queued")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Play a WAV on ESP32 via /api/v1/play")
    parser.add_argument("wav", type=Path)
    parser.add_argument("--esp", default="http://192.168.100.217")
    parser.add_argument("--uid", default="espbox-90e5b1d65984")
    parser.add_argument("--chunk-bytes", type=int, default=10 * 1024)
    parser.add_argument("--auth-token", default="")
    args = parser.parse_args()
    return play_wav(
        esp_url=args.esp,
        device_uid=args.uid,
        wav_path=args.wav,
        chunk_bytes=args.chunk_bytes,
        auth_token=args.auth_token,
    )


if __name__ == "__main__":
    raise SystemExit(main())
