"""Background speech transcription with faster-whisper (streaming + final)."""

from __future__ import annotations

import queue
import threading
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Literal

import numpy as np

try:
    from faster_whisper import WhisperModel
except ImportError:
    WhisperModel = None  # type: ignore[misc, assignment]


@dataclass
class _UtteranceAsrState:
    pcm: bytearray = field(default_factory=bytearray)
    sample_rate_hz: int = 16000
    channels: int = 1
    last_emitted_end_s: float = 0.0
    emitted_text: str = ""
    last_flush_pcm_bytes: int = 0
    in_flight: bool = False
    pending_flush: bool = False
    finalized: bool = False


@dataclass(frozen=True)
class _AsrJob:
    kind: Literal["partial", "final"]
    utterance_id: str


class WhisperTranscriber:
    """faster-whisper with incremental PCM feeding during upload."""

    def __init__(
        self,
        *,
        model_size: str = "base",
        device: str = "cuda",
        compute_type: str = "float16",
        beam_size: int = 1,
        flush_ms: int = 1200,
        streaming: bool = True,
        language: str | None = "en",
        on_final: Callable[[str, str], None] | None = None,
    ) -> None:
        if WhisperModel is None:
            raise RuntimeError("faster-whisper is not installed (pip install faster-whisper)")

        self._model_size = model_size
        self._device = device
        self._compute_type = compute_type
        self._beam_size = beam_size
        self._flush_ms = max(200, flush_ms)
        self._streaming = streaming
        self._language = language
        self._on_final = on_final
        self._model: WhisperModel | None = None
        self._model_lock = threading.Lock()
        self._states: dict[str, _UtteranceAsrState] = {}
        self._states_lock = threading.Lock()
        self._queue: queue.Queue[_AsrJob | None] = queue.Queue()
        self._worker = threading.Thread(target=self._run, name="whisper-asr", daemon=True)
        self._started = False

    @property
    def language_label(self) -> str:
        return self._language or "auto"

    def start(self) -> None:
        if self._started:
            return
        self._started = True
        self._worker.start()

    def stop(self) -> None:
        if not self._started:
            return
        self._queue.put(None)
        self._worker.join(timeout=30)
        self._started = False

    def preload(self) -> None:
        """Load the whisper model at startup (blocks until ready)."""
        self._ensure_model()

    def begin_utterance(
        self,
        utterance_id: str,
        *,
        sample_rate_hz: int,
        channels: int,
    ) -> None:
        with self._states_lock:
            self._states[utterance_id] = _UtteranceAsrState(
                sample_rate_hz=sample_rate_hz,
                channels=channels,
            )

    def feed(
        self,
        utterance_id: str,
        pcm: bytes,
        *,
        sample_rate_hz: int,
        channels: int,
    ) -> None:
        if not self._streaming or not pcm:
            return
        if not self._started:
            self.start()

        with self._states_lock:
            state = self._states.get(utterance_id)
            if state is None:
                state = _UtteranceAsrState(
                    sample_rate_hz=sample_rate_hz,
                    channels=channels,
                )
                self._states[utterance_id] = state
            state.pcm.extend(pcm)
            state.sample_rate_hz = sample_rate_hz
            state.channels = channels

            bytes_per_sec = sample_rate_hz * channels * 2
            flush_bytes = max(640, int(bytes_per_sec * self._flush_ms / 1000))
            new_bytes = len(state.pcm) - state.last_flush_pcm_bytes
            if new_bytes < flush_bytes and not state.finalized:
                return

            if state.in_flight:
                state.pending_flush = True
                return

            state.last_flush_pcm_bytes = len(state.pcm)
            state.in_flight = True

        self._queue.put(_AsrJob(kind="partial", utterance_id=utterance_id))

    def finalize_utterance(self, utterance_id: str) -> None:
        if not self._started:
            self.start()

        with self._states_lock:
            state = self._states.get(utterance_id)
            if state is None:
                state = _UtteranceAsrState()
                self._states[utterance_id] = state
            state.finalized = True
            if state.in_flight:
                state.pending_flush = True
                return
            state.in_flight = True

        self._queue.put(_AsrJob(kind="final", utterance_id=utterance_id))

    def load_pcm_and_finalize(
        self,
        utterance_id: str,
        pcm: bytes,
        *,
        sample_rate_hz: int,
        channels: int,
    ) -> None:
        """Transcribe a complete PCM buffer (finalize-only mode)."""
        if not self._started:
            self.start()
        with self._states_lock:
            self._states[utterance_id] = _UtteranceAsrState(
                pcm=bytearray(pcm),
                sample_rate_hz=sample_rate_hz,
                channels=channels,
                finalized=True,
                in_flight=True,
                last_flush_pcm_bytes=len(pcm),
            )
        self._queue.put(_AsrJob(kind="final", utterance_id=utterance_id))

    def _ensure_model(self) -> WhisperModel:
        with self._model_lock:
            if self._model is None:
                print(
                    f"[asr] loading faster-whisper model={self._model_size} "
                    f"device={self._device} compute={self._compute_type} "
                    f"language={self.language_label}",
                    flush=True,
                )
                t0 = time.time()
                self._model = WhisperModel(
                    self._model_size,
                    device=self._device,
                    compute_type=self._compute_type,
                )
                print(f"[asr] model ready in {time.time() - t0:.1f}s", flush=True)
            return self._model

    def _snapshot_state(self, utterance_id: str) -> tuple[bytes, _UtteranceAsrState] | None:
        with self._states_lock:
            state = self._states.get(utterance_id)
            if state is None or not state.pcm:
                return None
            return bytes(state.pcm), state

    def _pcm_to_audio(self, pcm: bytes) -> np.ndarray:
        if not pcm:
            return np.array([], dtype=np.float32)
        audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
        return self._normalize_for_whisper(audio)

    def _normalize_for_whisper(self, audio: np.ndarray, target_peak: float = 0.35) -> np.ndarray:
        """Boost quiet BOX mic captures so Whisper sees usable levels."""
        if audio.size == 0:
            return audio
        peak = float(np.max(np.abs(audio)))
        if peak < 1e-5:
            return audio
        if peak >= target_peak:
            return audio
        gain = target_peak / peak
        return np.clip(audio * gain, -1.0, 1.0)

    def _transcribe_pcm(self, pcm: bytes, sample_rate_hz: int) -> tuple[list, object, float]:
        model = self._ensure_model()
        audio = self._pcm_to_audio(pcm)
        peak = float(np.max(np.abs(audio))) if audio.size else 0.0
        t0 = time.time()
        kwargs: dict = {
            "beam_size": self._beam_size,
            "vad_filter": True,
        }
        if self._language is not None:
            kwargs["language"] = self._language
        segments, info = model.transcribe(audio, **kwargs)
        segment_list = list(segments)
        if peak < 0.12 and audio.size:
            print(
                f"[asr] note: input peak={peak:.3f} after normalize — check mic gain / dropped upload chunks",
                flush=True,
            )
        return segment_list, info, time.time() - t0

    def _emit_new_segments(
        self,
        utterance_id: str,
        state: _UtteranceAsrState,
        segments: list,
        *,
        partial: bool,
        decode_s: float,
        info: object,
    ) -> None:
        new_parts: list[str] = []
        for segment in segments:
            text = (segment.text or "").strip()
            if not text:
                continue
            if segment.end <= state.last_emitted_end_s + 0.05:
                continue
            if segment.start < state.last_emitted_end_s - 0.25 and new_parts:
                continue
            new_parts.append(text)
            state.last_emitted_end_s = max(state.last_emitted_end_s, float(segment.end))

        if new_parts:
            chunk = " ".join(new_parts)
            state.emitted_text = (state.emitted_text + " " + chunk).strip()
            tag = "asr+" if partial else "asr"
            lang = self._language or getattr(info, "language", None) or "unknown"
            print(f"[{tag}] {utterance_id} ({lang}, {decode_s:.1f}s): {chunk}", flush=True)

    def _finish_job(self, utterance_id: str) -> None:
        follow_up: _AsrJob | None = None
        with self._states_lock:
            state = self._states.get(utterance_id)
            if state is None:
                return
            state.in_flight = False
            if state.pending_flush or (state.finalized and state.last_flush_pcm_bytes < len(state.pcm)):
                state.pending_flush = False
                state.last_flush_pcm_bytes = len(state.pcm)
                state.in_flight = True
                kind: Literal["partial", "final"] = "final" if state.finalized else "partial"
                follow_up = _AsrJob(kind=kind, utterance_id=utterance_id)
            elif state.finalized:
                final_text = state.emitted_text
                if final_text:
                    print(f"[asr] {utterance_id}: {final_text}", flush=True)
                else:
                    print(f"[asr] {utterance_id}: (no speech detected)", flush=True)
                if self._on_final is not None and final_text:
                    try:
                        self._on_final(utterance_id, final_text)
                    except Exception:
                        traceback.print_exc()
                del self._states[utterance_id]

        if follow_up is not None:
            self._queue.put(follow_up)

    def _process_job(self, job: _AsrJob) -> None:
        snapshot = self._snapshot_state(job.utterance_id)
        if snapshot is None:
            self._finish_job(job.utterance_id)
            return

        pcm, state = snapshot
        try:
            segments, info, decode_s = self._transcribe_pcm(pcm, state.sample_rate_hz)
            partial = job.kind == "partial" and not state.finalized
            self._emit_new_segments(
                job.utterance_id,
                state,
                segments,
                partial=partial,
                decode_s=decode_s,
                info=info,
            )
            if job.kind == "final" and not partial:
                full_text = "".join(segment.text for segment in segments).strip()
                if full_text and full_text != state.emitted_text:
                    lang = self._language or getattr(info, "language", None) or "unknown"
                    print(
                        f"[asr] {job.utterance_id} ({lang}, {decode_s:.1f}s): {full_text}",
                        flush=True,
                    )
                    state.emitted_text = full_text
        except Exception:
            print(f"[asr] {job.utterance_id}: transcription failed", flush=True)
            traceback.print_exc()
        finally:
            self._finish_job(job.utterance_id)

    def _run(self) -> None:
        while True:
            item = self._queue.get()
            try:
                if item is None:
                    return
                self._process_job(item)
            finally:
                self._queue.task_done()


def resolve_whisper_language(value: str) -> str | None:
    """Return a Whisper language code, or None to auto-detect."""
    cleaned = value.strip().lower()
    if cleaned in ("", "en"):
        return "en"
    if cleaned in ("auto", "detect", "autodetect"):
        return None
    return cleaned


def build_transcriber_from_args(
    args,
    *,
    on_final: Callable[[str, str], None] | None = None,
) -> WhisperTranscriber | None:
    if getattr(args, "no_whisper", False):
        return None
    return WhisperTranscriber(
        model_size=args.whisper_model,
        device=args.whisper_device,
        compute_type=args.whisper_compute_type,
        beam_size=args.whisper_beam_size,
        flush_ms=args.whisper_flush_ms,
        streaming=not getattr(args, "no_streaming_asr", False),
        language=resolve_whisper_language(getattr(args, "whisper_language", "en")),
        on_final=on_final,
    )
