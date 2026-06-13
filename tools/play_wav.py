#!/usr/bin/env python3
"""Stream a WAV file to ESP32 /api/v1/play in paced chunks."""

from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "test_server"))

from esp_playback import stream_pcm_to_esp


def play_wav(
    *,
    esp_url: str,
    device_uid: str,
    wav_path: Path,
    chunk_bytes: int = 8192,
    auth_token: str = "",
) -> int:
    with wave.open(str(wav_path), "rb") as w:
        pcm = w.readframes(w.getnframes())
        rate = w.getframerate()
        channels = w.getnchannels()

    bytes_per_sec = rate * channels * 2
    print(
        f"Playing {wav_path.name}: {len(pcm) / bytes_per_sec:.2f}s, "
        f"{len(pcm)} bytes, chunk={chunk_bytes}"
    )

    host = esp_url.replace("http://", "").replace("https://", "").rstrip("/")
    if ":" not in host:
        host = f"{host}:80"

    device_ip = host.split(":")[0]
    ok = stream_pcm_to_esp(
        device_ip,
        device_uid,
        pcm,
        rate,
        channels,
        auth_token,
        chunk_bytes=chunk_bytes,
        log_prefix="[play]",
    )
    if ok:
        print("Done")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Play a WAV on ESP32 via /api/v1/play")
    parser.add_argument("wav", type=Path)
    parser.add_argument("--esp", default="http://192.168.100.217")
    parser.add_argument("--uid", default="espbox-90e5b1d65984")
    parser.add_argument("--chunk-bytes", type=int, default=8192)
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
