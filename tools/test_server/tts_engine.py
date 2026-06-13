"""Background TTS with Chatterbox and ESP32 playback."""

from __future__ import annotations

import queue
import threading
import time
import traceback
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Literal

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
PlaybackFn = Callable[[str, str, bytes, int, int, str], bool]
DEFAULT_VOICE_WAV = Path(__file__).resolve().parent / "voices" / "karla.wav"
FALLBACK_VOICE_WAV = Path(__file__).resolve().parent / "voices" / "default_female.wav"


@dataclass(frozen=True)
class _TtsJob:
    utterance_id: str
    text: str
    device_uid: str
    device_ip: str
    prompt_wav_path: str | None
    auth_token: str


class ChatterboxTtsEngine:
    """Synthesize speech in a worker thread and POST PCM to the ESP /play API."""

    def __init__(
        self,
        *,
        model: ChatterboxModel = "english",
        device: str = "cuda",
        target_sample_rate_hz: int = 16000,
        playback_fn: PlaybackFn | None = None,
        voice_wav_path: str | None = None,
        min_prompt_seconds: float = 5.0,
    ) -> None:
        if librosa is None or torch is None:
            raise RuntimeError("chatterbox-tts dependencies are not installed")

        self._model_name = model
        self._device = device
        self._target_sample_rate_hz = target_sample_rate_hz
        self._playback_fn = playback_fn
        self._min_prompt_seconds = min_prompt_seconds
        self._voice_wav_path = self._validate_voice_wav(voice_wav_path)
        self._model = None
        self._model_lock = threading.Lock()
        self._queue: queue.Queue[_TtsJob | None] = queue.Queue()
        self._worker = threading.Thread(target=self._run, name="chatterbox-tts", daemon=True)
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
        self._worker.start()

    def stop(self) -> None:
        if not self._started:
            return
        self._queue.put(None)
        self._worker.join(timeout=120)
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
    ) -> None:
        cleaned = text.strip()
        if not cleaned or not device_uid or not device_ip:
            return
        if not self._started:
            self.start()
        self._queue.put(
            _TtsJob(
                utterance_id=utterance_id,
                text=cleaned,
                device_uid=device_uid,
                device_ip=device_ip,
                prompt_wav_path=prompt_wav_path,
                auth_token=auth_token,
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

    def _process_job(self, job: _TtsJob) -> None:
        try:
            pcm, sample_rate_hz, channels = self.synthesize_pcm(
                job.text,
                prompt_wav_path=job.prompt_wav_path,
            )
            duration_s = len(pcm) / (sample_rate_hz * channels * 2)
            print(
                f"[tts] {job.utterance_id}: {duration_s:.2f}s -> "
                f"{job.device_uid} @ {job.device_ip}",
                flush=True,
            )
            if self._playback_fn is None:
                print(f"[tts] {job.utterance_id}: playback disabled (no handler)", flush=True)
                return
            ok = self._playback_fn(
                job.device_ip,
                job.device_uid,
                pcm,
                sample_rate_hz,
                channels,
                job.auth_token,
            )
            if not ok:
                print(f"[tts] {job.utterance_id}: playback failed", flush=True)
        except Exception:
            print(f"[tts] {job.utterance_id}: synthesis failed", flush=True)
            traceback.print_exc()

    def _run(self) -> None:
        while True:
            item = self._queue.get()
            try:
                if item is None:
                    return
                self._process_job(item)
            finally:
                self._queue.task_done()


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
        model=getattr(args, "tts_model", "english"),
        device=getattr(args, "tts_device", "cuda"),
        target_sample_rate_hz=getattr(args, "tts_sample_rate_hz", 16000),
        playback_fn=playback_fn,
        voice_wav_path=voice_wav,
    )
