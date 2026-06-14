#!/usr/bin/env python3
"""Smoke test Chatterbox TTS (no ESP required)."""

from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

from ml_runtime import configure_ml_runtime
from tts_engine import ChatterboxTtsEngine, resolve_voice_wav_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Chatterbox TTS smoke test")
    parser.add_argument("text", nargs="?", default="Hello from the ESP32 voice server.")
    parser.add_argument(
        "--model",
        choices=("english", "turbo", "multilingual"),
        default="english",
    )
    parser.add_argument("--device", default="cuda")
    parser.add_argument(
        "--voice-wav",
        type=Path,
        default=None,
        help="Optional voice clone reference WAV",
    )
    parser.add_argument("--hf-token", default="")
    parser.add_argument("--out", type=Path, default=Path("tts_smoke.wav"))
    args = parser.parse_args()

    configure_ml_runtime(hf_token=args.hf_token)
    engine = ChatterboxTtsEngine(
        model=args.model,
        device=args.device,
        voice_wav_path=resolve_voice_wav_path(
            str(args.voice_wav) if args.voice_wav else None
        ),
    )
    engine.preload()
    pcm, rate, channels = engine.synthesize_pcm(args.text)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.out), "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(pcm)

    duration_s = len(pcm) / (rate * channels * 2)
    print(f"Wrote {args.out} ({duration_s:.2f}s, {rate} Hz, {channels} ch)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
