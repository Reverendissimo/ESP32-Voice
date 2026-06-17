"""OpenAI-compatible /v1/audio/* endpoints for shared bridge STT/TTS models."""

from __future__ import annotations

import cgi
import io
import json
import tempfile
import wave
from pathlib import Path
from typing import TYPE_CHECKING, Any

import numpy as np

from bridge_log import blog

if TYPE_CHECKING:
    from http.server import BaseHTTPRequestHandler

    from server import ServerConfig

OPENAI_AUDIO_TRANSCRIPTIONS = "/v1/audio/transcriptions"
OPENAI_AUDIO_SPEECH = "/v1/audio/speech"
OPENAI_AUDIO_PATHS = frozenset({OPENAI_AUDIO_TRANSCRIPTIONS, OPENAI_AUDIO_SPEECH})


def is_openai_audio_path(path: str) -> bool:
    normalized = path.rstrip("/") or "/"
    return normalized in OPENAI_AUDIO_PATHS


def _openai_error(message: str, *, status: int = 400, error_type: str = "invalid_request_error") -> tuple[int, dict[str, Any]]:
    return status, {
        "error": {
            "message": message,
            "type": error_type,
            "code": None,
        },
    }


def _parse_bearer_token(authorization: str) -> str:
    value = authorization.strip()
    if not value.lower().startswith("bearer "):
        return ""
    return value[7:].strip()


def _check_openai_auth(handler: BaseHTTPRequestHandler, cfg: ServerConfig) -> tuple[int, dict[str, Any]] | None:
    expected = cfg.voice_tools_openai_key.strip()
    if not expected:
        return _openai_error(
            "OpenAI audio tools are disabled (set VOICE_TOOLS_OPENAI_KEY)",
            status=503,
            error_type="server_error",
        )
    token = _parse_bearer_token(handler.headers.get("Authorization", ""))
    if token != expected:
        return _openai_error("Incorrect API key provided", status=401, error_type="invalid_request_error")
    return None


def _parse_multipart(handler: BaseHTTPRequestHandler) -> tuple[dict[str, str], bytes, str]:
    content_type = handler.headers.get("Content-Type", "")
    if "multipart/form-data" not in content_type.lower():
        raise ValueError("Expected multipart/form-data")

    length = int(handler.headers.get("Content-Length", "0") or "0")
    body = handler.rfile.read(length) if length > 0 else b""
    environ = {
        "REQUEST_METHOD": "POST",
        "CONTENT_TYPE": content_type,
        "CONTENT_LENGTH": str(len(body)),
    }
    form = cgi.FieldStorage(fp=io.BytesIO(body), environ=environ, keep_blank_values=True)

    fields: dict[str, str] = {}
    file_data = b""
    file_name = "audio.wav"
    for key in form.keys():
        item = form[key]
        if isinstance(item, list):
            items = item
        else:
            items = [item]
        for part in items:
            if getattr(part, "filename", None):
                file_data = part.file.read()
                file_name = str(part.filename or file_name)
            else:
                fields[key] = str(part.value or "")
    if not file_data:
        raise ValueError("Missing required file field")
    return fields, file_data, file_name


def _decode_upload_to_pcm16_mono(data: bytes, filename: str, *, target_sr: int = 16000) -> tuple[bytes, int]:
    suffix = Path(filename).suffix.lower() or ".wav"
    if suffix == ".wav":
        try:
            with wave.open(io.BytesIO(data), "rb") as wf:
                channels = wf.getnchannels()
                sample_width = wf.getsampwidth()
                rate = wf.getframerate()
                frames = wf.readframes(wf.getnframes())
            if sample_width != 2:
                raise ValueError(f"unsupported wav sample width: {sample_width}")

            audio = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
            if channels > 1:
                audio = audio.reshape(-1, channels).mean(axis=1)
            if rate != target_sr:
                import librosa

                audio = librosa.resample(audio, orig_sr=rate, target_sr=target_sr)
            pcm16 = (np.clip(audio, -1.0, 1.0) * 32767.0).astype(np.int16)
            return pcm16.tobytes(), target_sr
        except (wave.Error, ValueError):
            pass

    import librosa

    decode_suffix = suffix if suffix else ".opus"
    with tempfile.NamedTemporaryFile(suffix=decode_suffix) as tmp:
        tmp.write(data)
        tmp.flush()
        audio, sr = librosa.load(tmp.name, sr=target_sr, mono=True)

    pcm16 = (np.clip(audio, -1.0, 1.0) * 32767.0).astype(np.int16)
    return pcm16.tobytes(), int(sr)


def pcm16_to_wav(pcm: bytes, sample_rate_hz: int, channels: int = 1) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate_hz)
        wf.writeframes(pcm)
    return buf.getvalue()


def pcm16_to_opus(
    pcm: bytes,
    sample_rate_hz: int,
    channels: int = 1,
    *,
    bitrate: str = "64k",
) -> bytes:
    """Encode mono/stereo PCM16 to Opus (Ogg container) via ffmpeg/pydub."""
    try:
        from pydub import AudioSegment
    except ImportError as exc:
        raise RuntimeError("pydub is required for opus output (pip install pydub)") from exc

    segment = AudioSegment(
        data=pcm,
        sample_width=2,
        frame_rate=sample_rate_hz,
        channels=channels,
    )
    out = io.BytesIO()
    segment.export(out, format="opus", bitrate=bitrate, codec="libopus")
    return out.getvalue()


_OPENAI_SPEECH_FORMATS = frozenset({"wav", "pcm", "opus", "ogg"})
_OPENAI_SPEECH_CONTENT_TYPES = {
    "wav": "audio/wav",
    "pcm": "audio/pcm",
    "opus": "audio/opus",
    "ogg": "audio/ogg",
}


def handle_openai_audio_post(handler: BaseHTTPRequestHandler, cfg: ServerConfig, path: str) -> None:
    normalized = path.rstrip("/") or "/"
    auth_err = _check_openai_auth(handler, cfg)
    if auth_err is not None:
        _send_openai_json(handler, *auth_err)
        return

    if normalized == OPENAI_AUDIO_TRANSCRIPTIONS:
        _handle_transcriptions(handler, cfg)
        return
    if normalized == OPENAI_AUDIO_SPEECH:
        _handle_speech(handler, cfg)
        return

    _send_openai_json(handler, *_openai_error("Unknown route", status=404))


def _handle_transcriptions(handler: BaseHTTPRequestHandler, cfg: ServerConfig) -> None:
    if cfg.transcriber is None:
        _send_openai_json(handler, *_openai_error("Speech-to-text is disabled on this bridge", status=503, error_type="server_error"))
        return

    try:
        fields, file_data, file_name = _parse_multipart(handler)
    except ValueError as exc:
        _send_openai_json(handler, *_openai_error(str(exc)))
        return

    model = fields.get("model", "").strip()
    if not model:
        _send_openai_json(handler, *_openai_error("Missing required field: model"))
        return
    if model != cfg.openai_stt_model:
        _send_openai_json(
            handler,
            *_openai_error(
                f"Unknown model {model!r} (this bridge serves {cfg.openai_stt_model!r})",
            ),
        )
        return

    response_format = fields.get("response_format", "json").strip().lower() or "json"
    if response_format not in ("json", "text", "verbose_json"):
        _send_openai_json(handler, *_openai_error(f"Unsupported response_format: {response_format}"))

    language = fields.get("language", "").strip() or None

    try:
        pcm, sample_rate_hz = _decode_upload_to_pcm16_mono(file_data, file_name)
        text = cfg.transcriber.transcribe_pcm_sync(
            pcm,
            sample_rate_hz=sample_rate_hz,
            language=language,
        )
    except Exception as exc:
        blog(f"[openai-stt] transcription failed: {exc}")
        _send_openai_json(
            handler,
            *_openai_error(f"Transcription failed: {exc}", status=500, error_type="server_error"),
        )
        return

    if response_format == "text":
        body = text.encode("utf-8")
        handler.send_response(200)
        handler.send_header("Content-Type", "text/plain; charset=utf-8")
        handler.send_header("Content-Length", str(len(body)))
        handler.end_headers()
        handler.wfile.write(body)
        return

    if response_format == "verbose_json":
        payload = {"text": text, "language": language or cfg.transcriber.language_label, "duration": None, "words": []}
    else:
        payload = {"text": text}

    _send_openai_json(handler, 200, payload)


def _handle_speech(handler: BaseHTTPRequestHandler, cfg: ServerConfig) -> None:
    if cfg.tts is None:
        _send_openai_json(handler, *_openai_error("Text-to-speech is disabled on this bridge", status=503, error_type="server_error"))
        return

    length = int(handler.headers.get("Content-Length", "0") or "0")
    raw = handler.rfile.read(length) if length > 0 else b"{}"
    try:
        payload = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError:
        _send_openai_json(handler, *_openai_error("Invalid JSON body"))
        return
    if not isinstance(payload, dict):
        _send_openai_json(handler, *_openai_error("JSON body must be an object"))
        return

    model = str(payload.get("model", "")).strip()
    text = str(payload.get("input", "")).strip()
    if not model:
        _send_openai_json(handler, *_openai_error("Missing required field: model"))
        return
    if model != cfg.openai_tts_model:
        _send_openai_json(
            handler,
            *_openai_error(
                f"Unknown model {model!r} (this bridge serves {cfg.openai_tts_model!r})",
            ),
        )
        return
    if not text:
        _send_openai_json(handler, *_openai_error("Missing required field: input"))
        return

    response_format = str(payload.get("response_format", "opus")).strip().lower() or "opus"
    if response_format not in _OPENAI_SPEECH_FORMATS:
        _send_openai_json(
            handler,
            *_openai_error(
                f"Unsupported response_format {response_format!r} "
                f"(supported: {', '.join(sorted(_OPENAI_SPEECH_FORMATS))})",
            ),
        )
        return

    # OpenAI requires voice; we accept it but use the bridge-configured clone/builtin.
    _ = payload.get("voice")

    try:
        pcm, sample_rate_hz, channels = cfg.tts.synthesize_pcm(text)
    except Exception as exc:
        blog(f"[openai-tts] synthesis failed: {exc}")
        _send_openai_json(
            handler,
            *_openai_error(f"Speech synthesis failed: {exc}", status=500, error_type="server_error"),
        )
        return

    if response_format == "pcm":
        body = pcm
    elif response_format == "wav":
        body = pcm16_to_wav(pcm, sample_rate_hz, channels)
    else:
        try:
            body = pcm16_to_opus(pcm, sample_rate_hz, channels)
        except Exception as exc:
            blog(f"[openai-tts] opus encode failed: {exc}")
            _send_openai_json(
                handler,
                *_openai_error(f"Opus encoding failed: {exc}", status=500, error_type="server_error"),
            )
            return
    content_type = _OPENAI_SPEECH_CONTENT_TYPES[response_format]

    handler.send_response(200)
    handler.send_header("Content-Type", content_type)
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def _send_openai_json(handler: BaseHTTPRequestHandler, code: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload).encode("utf-8")
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)
