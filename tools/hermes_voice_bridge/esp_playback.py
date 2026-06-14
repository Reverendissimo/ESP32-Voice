"""Paced PCM streaming to ESP32 /api/v1/play with ring-buffer flow control."""

from __future__ import annotations

import base64
import time
from typing import Any

try:
    import requests
except ImportError:
    requests = None  # type: ignore[assignment]

RING_CAPACITY_BYTES = 320 * 1024
# ~384 ms of PCM per chunk at 16 kHz mono.
SLOW_CHUNK_MS = 180
DEFAULT_CHUNK_BYTES = 12288


def _ring_stats(payload: dict) -> tuple[int, int, int]:
    used = int(payload.get("ring_used_bytes", 0))
    free = int(payload.get("ring_free_bytes", RING_CAPACITY_BYTES - used))
    capacity = int(payload.get("ring_capacity_bytes", RING_CAPACITY_BYTES))
    return used, free, capacity


class EspPlayStream:
    """HTTP keep-alive session for continuous /play chunk streaming."""

    def __init__(
        self,
        device_ip: str,
        device_uid: str,
        sample_rate_hz: int,
        channels: int,
        auth_token: str = "",
        *,
        chunk_bytes: int = DEFAULT_CHUNK_BYTES,
        low_water_bytes: int = 0,
        max_retries: int = 30,
        retry_sleep_s: float = 0.1,
        request_timeout_s: float = 30.0,
        log_prefix: str = "[play]",
    ) -> None:
        if requests is None:
            raise RuntimeError("requests is not installed")

        self._url = f"http://{device_ip}/api/v1/play"
        self._device_uid = device_uid
        self._sample_rate_hz = sample_rate_hz
        self._channels = channels
        self._auth_token = auth_token
        self._chunk_bytes = max(2, chunk_bytes - (chunk_bytes % 2))
        self._max_retries = max_retries
        self._retry_sleep_s = retry_sleep_s
        self._request_timeout_s = request_timeout_s
        self._log_prefix = log_prefix
        self._bytes_per_sec = max(1, sample_rate_hz * channels * 2)
        self._chunk_idx = 0
        self._ring_used = 0
        self._ring_free = RING_CAPACITY_BYTES
        self._ring_capacity = RING_CAPACITY_BYTES
        self._session = requests.Session()
        self._headers: dict[str, str] = {
            "Content-Type": "application/json",
            "Connection": "keep-alive",
        }
        if auth_token:
            self._headers["X-Auth-Token"] = auth_token

    def close(self) -> None:
        self._session.close()

    def append_pcm(self, pcm: bytes, *, stream_end: bool) -> bool:
        if not pcm:
            if not stream_end:
                return True
            return self._post_chunk(b"", stream_end=True)

        total = len(pcm)
        sent = 0
        ok = True

        while sent < total:
            piece = pcm[sent : sent + self._chunk_bytes]
            needed = len(piece)
            while self._ring_free < needed:
                time.sleep(0.01)
                self._ring_used = max(0, self._ring_used - int(self._bytes_per_sec * 0.01))
                self._ring_free = self._ring_capacity - self._ring_used

            is_last = sent + len(piece) >= total
            end_stream = bool(stream_end and is_last)
            if not self._post_chunk(piece, stream_end=end_stream):
                ok = False
                break

            sent += len(piece)
            self._chunk_idx += 1

        return ok

    def _post_chunk(self, piece: bytes, *, stream_end: bool) -> bool:
        body: dict[str, Any] = {
            "v": 1,
            "target_device_uid": self._device_uid,
            "request_id": f"{self._log_prefix.strip('[]')}_{self._chunk_idx}",
            "command_id": f"chunk_{self._chunk_idx}",
            "sample_rate_hz": self._sample_rate_hz,
            "channels": self._channels,
            "pcm_b64": base64.b64encode(piece).decode("ascii"),
            "stream_end": stream_end,
        }

        for attempt in range(self._max_retries):
            t0 = time.monotonic()
            try:
                resp = self._session.post(
                    self._url,
                    json=body,
                    headers=self._headers,
                    timeout=self._request_timeout_s,
                )
            except requests.RequestException as exc:
                wait = min(self._retry_sleep_s * (attempt + 1), 1.0)
                print(
                    f"{self._log_prefix} chunk {self._chunk_idx} error: {exc} "
                    f"(retry in {wait:.1f}s)",
                    flush=True,
                )
                time.sleep(wait)
                continue

            elapsed_ms = (time.monotonic() - t0) * 1000.0

            if resp.status_code == 200:
                try:
                    payload = resp.json()
                    self._ring_used, self._ring_free, self._ring_capacity = _ring_stats(payload)
                except ValueError:
                    self._ring_free = max(0, self._ring_free - len(piece))
                    self._ring_used = max(0, self._ring_capacity - self._ring_free)
                keep_alive = resp.headers.get("Connection", "").lower()
                msg = (
                    f"{self._log_prefix} chunk {self._chunk_idx} ok "
                    f"({len(piece)} B, {elapsed_ms:.0f} ms, "
                    f"ring_used={self._ring_used}, ring_free={self._ring_free}"
                )
                if keep_alive:
                    msg += f", conn={keep_alive}"
                msg += ")"
                print(msg, flush=True)
                if elapsed_ms > SLOW_CHUNK_MS:
                    print(
                        f"{self._log_prefix} WARN chunk {self._chunk_idx} slow "
                        f"({elapsed_ms:.0f} ms > {SLOW_CHUNK_MS} ms) — playback may stutter",
                        flush=True,
                    )
                return True

            if resp.status_code == 503:
                self._ring_free = 0
                self._ring_used = self._ring_capacity
                time.sleep(min(self._retry_sleep_s * (attempt + 1), 1.0))
                continue

            print(
                f"{self._log_prefix} chunk {self._chunk_idx} HTTP {resp.status_code} "
                f"({len(piece)} bytes, {elapsed_ms:.0f} ms)",
                flush=True,
            )
            return False

        print(
            f"{self._log_prefix} chunk {self._chunk_idx}: playback stalled after retries",
            flush=True,
        )
        return False


def stream_pcm_to_esp(
    device_ip: str,
    device_uid: str,
    pcm: bytes,
    sample_rate_hz: int,
    channels: int,
    auth_token: str = "",
    *,
    chunk_bytes: int = DEFAULT_CHUNK_BYTES,
    low_water_bytes: int = 0,
    max_retries: int = 30,
    retry_sleep_s: float = 0.1,
    request_timeout_s: float = 30.0,
    stream_end: bool | None = None,
    log_prefix: str = "[play]",
) -> bool:
    """Stream PCM using device ring-buffer feedback for flow control."""
    if requests is None:
        print(f"{log_prefix} requests not installed; skip playback", flush=True)
        return False

    end_stream = True if stream_end is None else stream_end
    stream = EspPlayStream(
        device_ip,
        device_uid,
        sample_rate_hz,
        channels,
        auth_token,
        chunk_bytes=chunk_bytes,
        low_water_bytes=low_water_bytes,
        max_retries=max_retries,
        retry_sleep_s=retry_sleep_s,
        request_timeout_s=request_timeout_s,
        log_prefix=log_prefix,
    )
    try:
        return stream.append_pcm(pcm, stream_end=end_stream)
    finally:
        stream.close()
