#!/usr/bin/env python3
"""Smoke-test faster-whisper transcription on a saved WAV."""

from __future__ import annotations

import argparse
import sys
import time
import wave
from pathlib import Path

from transcriber import WhisperTranscriber, resolve_whisper_language


def main() -> int:
    parser = argparse.ArgumentParser(description="Transcribe a WAV with faster-whisper")
    parser.add_argument("wav", type=Path)
    parser.add_argument("--model", default="base")
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--compute-type", default="float16")
    parser.add_argument("--chunk-ms", type=int, default=1200)
    parser.add_argument(
        "--language",
        default="en",
        help="Whisper language code (default: en). Use 'auto' to detect.",
    )
    args = parser.parse_args()

    if not args.wav.is_file():
        print(f"missing file: {args.wav}", file=sys.stderr)
        return 1

    with wave.open(str(args.wav), "rb") as wf:
        pcm = wf.readframes(wf.getnframes())
        rate = wf.getframerate()
        channels = wf.getnchannels()

    transcriber = WhisperTranscriber(
        model_size=args.model,
        device=args.device,
        compute_type=args.compute_type,
        flush_ms=args.chunk_ms,
        language=resolve_whisper_language(args.language),
    )
    transcriber.preload()
    transcriber.start()
    transcriber.begin_utterance("smoke", sample_rate_hz=rate, channels=channels)

    bytes_per_sec = rate * channels * 2
    chunk_bytes = max(640, int(bytes_per_sec * args.chunk_ms / 1000))
    for offset in range(0, len(pcm), chunk_bytes):
        transcriber.feed("smoke", pcm[offset : offset + chunk_bytes], sample_rate_hz=rate, channels=channels)
        time.sleep(args.chunk_ms / 1000.0)

    transcriber.finalize_utterance("smoke")
    time.sleep(30)
    transcriber.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
