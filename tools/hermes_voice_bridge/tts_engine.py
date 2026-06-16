"""Background TTS with Chatterbox and ESP32 playback."""

from __future__ import annotations

import queue
import re
import threading
import time
import traceback
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Literal

from esp_playback import EspPlayStream
from ml_runtime import configure_chatterbox_attention, configure_ml_runtime, quiet_hf_hub_noise

configure_ml_runtime()

import numpy as np

try:
    import librosa
except ImportError:
    librosa = None  # type: ignore[assignment]

try:
    import torch
except ImportError:
    torch = None  # type: ignore[assignment]

ChatterboxModel = Literal["english", "turbo", "multilingual"]
PlaybackFn = Callable[..., bool]
DEFAULT_VOICE_WAV = Path(__file__).resolve().parent / "voices" / "karla.wav"
FALLBACK_VOICE_WAV = Path(__file__).resolve().parent / "voices" / "default_female.wav"

# Chatterbox Turbo only — other models speak these as literal words.
PARALINGUISTIC_TAGS = (
    "[clear throat]",
    "[chuckle]",
    "[cough]",
    "[gasp]",
    "[groan]",
    "[laugh]",
    "[shush]",
    "[sigh]",
    "[sniff]",
)
_PARALINGUISTIC_TAG_RE = re.compile(
    r"\[(?:"
    + "|".join(re.escape(tag.strip("[]")) for tag in PARALINGUISTIC_TAGS)
    + r")\]",
    re.IGNORECASE,
)


def strip_paralinguistic_tags(text: str) -> str:
    """Remove Turbo-only tags so non-turbo models do not say 'laugh' aloud."""
    cleaned = _PARALINGUISTIC_TAG_RE.sub("", text)
    return re.sub(r"\s{2,}", " ", cleaned).strip()


@dataclass(frozen=True)
class _TtsJob:
    utterance_id: str
    text: str
    device_uid: str
    device_ip: str
    prompt_wav_path: str | None
    auth_token: str
    stream_end: bool = True
    end_marker: bool = False


@dataclass(frozen=True)
class _SynthResult:
    job: _TtsJob
    pcm: bytes
    sample_rate_hz: int
    channels: int


class ChatterboxTtsEngine:
    """Synthesize and play speech with pipelined synth + playback worker threads."""

    def __init__(
        self,
        *,
        model: ChatterboxModel = "english",
        device: str = "cuda",
        target_sample_rate_hz: int = 16000,
        playback_fn: PlaybackFn | None = None,
        voice_wav_path: str | None = None,
        min_prompt_seconds: float = 5.0,
        play_chunk_bytes: int = 24576,
    ) -> None:
        if librosa is None or torch is None:
            raise RuntimeError("chatterbox-tts dependencies are not installed")

        self._model_name = model
        self._device = device
        self._target_sample_rate_hz = target_sample_rate_hz
        self._playback_fn = playback_fn
        self._play_chunk_bytes = max(2, play_chunk_bytes - (play_chunk_bytes % 2))
        self._min_prompt_seconds = min_prompt_seconds
        self._play_streams: dict[str, EspPlayStream] = {}
        self._play_streams_lock = threading.Lock()
        self._voice_wav_path = self._validate_voice_wav(voice_wav_path)
        self._model = None
        self._model_lock = threading.Lock()
        self._job_queue: queue.Queue[_TtsJob | None] = queue.Queue()
        self._ready_queue: queue.Queue[_SynthResult | None] = queue.Queue()
        self._synth_worker = threading.Thread(
            target=self._synth_loop, name="chatterbox-tts-synth", daemon=True
        )
        self._play_worker = threading.Thread(
            target=self._play_loop, name="chatterbox-tts-play", daemon=True
        )
        self._started = False

    @property
    def voice_label(self) -> str:
        if not self._voice_wav_path:
            return "builtin"
        if Path(self._voice_wav_path).resolve() == DEFAULT_VOICE_WAV.resolve():
            return "karla (default)"
        if Path(self._voice_wav_path).resolve() == FALLBACK_VOICE_WAV.resolve():
            return "female (fallback)"
        return f"clone ({self._voice_wav_path})"

    def start(self) -> None:
        if self._started:
            return
        self._started = True
        self._synth_worker.start()
        self._play_worker.start()

    def stop(self) -> None:
        if not self._started:
            return
        self._job_queue.put(None)
        self._synth_worker.join(timeout=120)
        self._play_worker.join(timeout=120)
        self._started = False

    def preload(self) -> None:
        self._ensure_model()

    def speak_async(
        self,
        utterance_id: str,
        text: str,
        *,
        device_uid: str,
        device_ip: str,
        prompt_wav_path: str | None = None,
        auth_token: str = "",
        stream_end: bool = True,
    ) -> None:
        cleaned = text.strip()
        if not cleaned or not device_uid or not device_ip:
            return
        if not self._started:
            self.start()
        self._job_queue.put(
            _TtsJob(
                utterance_id=utterance_id,
                text=cleaned,
                device_uid=device_uid,
                device_ip=device_ip,
                prompt_wav_path=prompt_wav_path,
                auth_token=auth_token,
                stream_end=stream_end,
            )
        )

    def speak_stream_end(
        self,
        utterance_id: str,
        *,
        device_uid: str,
        device_ip: str,
        auth_token: str = "",
    ) -> None:
        if not utterance_id or not device_uid or not device_ip:
            return
        if not self._started:
            self.start()
        self._job_queue.put(
            _TtsJob(
                utterance_id=utterance_id,
                text="",
                device_uid=device_uid,
                device_ip=device_ip,
                prompt_wav_path=None,
                auth_token=auth_token,
                stream_end=True,
                end_marker=True,
            )
        )

    def synthesize_pcm(
        self,
        text: str,
        *,
        prompt_wav_path: str | None = None,
    ) -> tuple[bytes, int, int]:
        wav, src_sr = self._synthesize_wav(text, prompt_wav_path=prompt_wav_path)
        return self._wav_to_pcm16(wav, src_sr)

    def _validate_voice_wav(self, voice_wav_path: str | None) -> str | None:
        if not voice_wav_path:
            return None
        path = Path(voice_wav_path)
        if not path.is_file():
            print(f"[tts] voice wav not found: {path} (using default voice)", flush=True)
            return None
        validated = self._prompt_long_enough(str(path))
        if validated is None:
            print(
                f"[tts] voice wav too short for {self._model_name} "
                f"(need >= {self._min_prompt_seconds:.0f}s): {path} (using default voice)",
                flush=True,
            )
            return None
        print(f"[tts] voice clone -> {path}", flush=True)
        return validated

    def _ensure_model(self) -> object:
        with self._model_lock:
            if self._model is not None:
                return self._model

            print(
                f"[tts] loading chatterbox model={self._model_name} device={self._device} "
                f"voice={self.voice_label}",
                flush=True,
            )
            t0 = time.time()
            with quiet_hf_hub_noise():
                if self._model_name == "turbo":
                    from chatterbox.tts_turbo import ChatterboxTurboTTS

                    self._model = ChatterboxTurboTTS.from_pretrained(device=self._device)
                elif self._model_name == "multilingual":
                    from chatterbox.mtl_tts import ChatterboxMultilingualTTS

                    self._model = ChatterboxMultilingualTTS.from_pretrained(device=self._device)
                else:
                    from chatterbox.tts import ChatterboxTTS

                    self._model = ChatterboxTTS.from_pretrained(device=self._device)

            configure_chatterbox_attention(self._model)

            print(f"[tts] model ready in {time.time() - t0:.1f}s", flush=True)
            return self._model

    def _prompt_long_enough(self, prompt_wav_path: str | None) -> str | None:
        if not prompt_wav_path:
            return None
        path = Path(prompt_wav_path)
        if not path.is_file():
            return None
        try:
            with wave.open(str(path), "rb") as wf:
                frames = wf.getnframes()
                rate = wf.getframerate()
            if rate <= 0:
                return None
            duration_s = frames / rate
            if self._model_name == "turbo" and duration_s + 0.05 < self._min_prompt_seconds:
                return None
            return str(path)
        except wave.Error:
            return None

    def _resolve_prompt(self, override: str | None) -> str | None:
        if override:
            return self._prompt_long_enough(override)
        return self._voice_wav_path

    def _synthesize_wav(self, text: str, *, prompt_wav_path: str | None) -> tuple[object, int]:
        model = self._ensure_model()
        prompt = self._resolve_prompt(prompt_wav_path)
        if self._model_name != "turbo":
            text = strip_paralinguistic_tags(text)
            if not text:
                raise ValueError("empty text after removing paralinguistic tags")
        t0 = time.time()

        if self._model_name == "multilingual":
            wav = model.generate(text, language_id="en")
        elif prompt:
            wav = model.generate(text, audio_prompt_path=prompt)
        else:
            wav = model.generate(text)

        src_sr = int(getattr(model, "sr", self._target_sample_rate_hz))
        print(f"[tts] synthesized {len(text)} chars in {time.time() - t0:.1f}s", flush=True)
        return wav, src_sr

    def _wav_to_pcm16(self, wav: object, src_sr: int) -> tuple[bytes, int, int]:
        if torch is not None and isinstance(wav, torch.Tensor):
            audio = wav.detach().cpu().float().numpy()
        else:
            audio = np.asarray(wav, dtype=np.float32)

        audio = np.squeeze(audio)
        if audio.ndim != 1:
            audio = audio.reshape(-1)

        target_sr = self._target_sample_rate_hz
        if src_sr != target_sr:
            audio = librosa.resample(audio, orig_sr=src_sr, target_sr=target_sr)

        audio = np.clip(audio, -1.0, 1.0)
        pcm16 = (audio * 32767.0).astype(np.int16)
        return pcm16.tobytes(), target_sr, 1

    def _synth_loop(self) -> None:
        while True:
            job = self._job_queue.get()
            try:
                if job is None:
                    self._ready_queue.put(None)
                    return
                if job.end_marker:
                    self._ready_queue.put(
                        _SynthResult(
                            job=job,
                            pcm=b"",
                            sample_rate_hz=self._target_sample_rate_hz,
                            channels=1,
                        )
                    )
                    continue
                try:
                    pcm, sample_rate_hz, channels = self.synthesize_pcm(
                        job.text,
                        prompt_wav_path=job.prompt_wav_path,
                    )
                    self._ready_queue.put(
                        _SynthResult(
                            job=job,
                            pcm=pcm,
                            sample_rate_hz=sample_rate_hz,
                            channels=channels,
                        )
                    )
                except Exception:
                    print(f"[tts] {job.utterance_id}: synthesis failed", flush=True)
                    traceback.print_exc()
            finally:
                self._job_queue.task_done()

    def _play_loop(self) -> None:
        while True:
            item = self._ready_queue.get()
            try:
                if item is None:
                    return
                self._play_result(item)
            finally:
                self._ready_queue.task_done()

    def _play_stream_for(self, job: _TtsJob) -> EspPlayStream:
        with self._play_streams_lock:
            stream = self._play_streams.get(job.utterance_id)
            if stream is None:
                stream = EspPlayStream(
                    job.device_ip,
                    job.device_uid,
                    self._target_sample_rate_hz,
                    1,
                    job.auth_token,
                    chunk_bytes=self._play_chunk_bytes,
                    log_prefix="[tts]",
                )
                self._play_streams[job.utterance_id] = stream
            else:
                stream.update_target(job.device_ip, job.device_uid, job.auth_token)
            return stream

    def _close_play_stream(self, utterance_id: str) -> None:
        with self._play_streams_lock:
            stream = self._play_streams.pop(utterance_id, None)
        if stream is not None:
            stream.close()

    def _play_result(self, result: _SynthResult) -> None:
        job = result.job
        pcm = result.pcm
        sample_rate_hz = result.sample_rate_hz
        channels = result.channels
        duration_s = len(pcm) / (sample_rate_hz * channels * 2)
        if job.end_marker:
            print(f"[tts] {job.utterance_id}: stream end", flush=True)
        else:
            print(
                f"[tts] {job.utterance_id}: {duration_s:.2f}s -> "
                f"{job.device_uid} @ {job.device_ip}",
                flush=True,
            )
        if self._playback_fn is None and not job.end_marker:
            print(f"[tts] {job.utterance_id}: playback disabled (no handler)", flush=True)
            return

        stream = self._play_stream_for(job)
        ok = stream.append_pcm(pcm, stream_end=job.stream_end)
        if job.stream_end:
            self._close_play_stream(job.utterance_id)
        if not ok:
            print(f"[tts] {job.utterance_id}: playback failed", flush=True)
            self._close_play_stream(job.utterance_id)


def resolve_voice_wav_path(explicit: str | None) -> str | None:
    """Pick a Chatterbox clone reference WAV."""
    if explicit:
        return explicit
    if DEFAULT_VOICE_WAV.is_file():
        return str(DEFAULT_VOICE_WAV)
    if FALLBACK_VOICE_WAV.is_file():
        return str(FALLBACK_VOICE_WAV)
    return None


def build_tts_from_args(args, *, playback_fn: PlaybackFn | None) -> ChatterboxTtsEngine | None:
    if getattr(args, "no_tts", False):
        return None
    explicit = getattr(args, "tts_voice_wav", "") or ""
    if getattr(args, "tts_builtin_voice", False):
        voice_wav = None
    else:
        voice_wav = resolve_voice_wav_path(explicit or None)
    return ChatterboxTtsEngine(
        model=getattr(args, "tts_model", "turbo"),
        device=getattr(args, "tts_device", "cuda"),
        target_sample_rate_hz=getattr(args, "tts_sample_rate_hz", 16000),
        playback_fn=playback_fn,
        voice_wav_path=voice_wav,
        play_chunk_bytes=getattr(args, "play_chunk_bytes", 24576),
    )
