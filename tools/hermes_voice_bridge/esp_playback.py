"""Paced PCM streaming to ESP32 /api/v1/play with ring-buffer flow control."""

from __future__ import annotations

import base64
import time
from typing import Any

try:
    import requests
except ImportError:
    requests = None  # type: ignore[assignment]

from bridge_log import blog

RING_CAPACITY_BYTES = 192 * 1024
# ~256 ms of PCM per chunk at 16 kHz mono — smaller posts reduce time-to-first-sample on ESP.
SLOW_CHUNK_MS = 400
DEFAULT_CHUNK_BYTES = 8192
HIGH_WATER_BYTES = 96 * 1024
MIN_SEND_BYTES = 4096
RING_WAIT_TIMEOUT_S = 5.0


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
        self._binary_play = True
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

    def update_target(self, device_ip: str, device_uid: str, auth_token: str = "") -> None:
        self._url = f"http://{device_ip}/api/v1/play"
        self._device_uid = device_uid
        self._auth_token = auth_token
        if auth_token:
            self._headers["X-Auth-Token"] = auth_token
        else:
            self._headers.pop("X-Auth-Token", None)

    def _reset_ring_stats(self) -> None:
        self._ring_used = 0
        self._ring_free = self._ring_capacity

    def _estimate_ring_drain(self, elapsed_s: float) -> None:
        if elapsed_s <= 0:
            return
        drained = int(self._bytes_per_sec * elapsed_s)
        if drained <= 0:
            return
        self._ring_used = max(0, self._ring_used - drained)
        self._ring_free = min(self._ring_capacity, self._ring_free + drained)

    def _wait_for_send_space(self, want_bytes: int) -> int:
        """Wait until at least one even-sized slice fits; return sendable bytes (<= want)."""
        want_bytes = max(2, want_bytes - (want_bytes % 2))
        wait_start = time.monotonic()
        last = wait_start
        while True:
            avail = self._ring_free - (self._ring_free % 2)
            send_len = min(want_bytes, avail)
            if send_len >= want_bytes:
                return send_len
            if send_len >= MIN_SEND_BYTES:
                return send_len
            if avail >= 2 and want_bytes <= MIN_SEND_BYTES:
                return avail

            now = time.monotonic()
            self._estimate_ring_drain(now - last)
            last = now

            if now - wait_start > RING_WAIT_TIMEOUT_S:
                if avail >= 2:
                    return avail
                blog(
                    f"{self._log_prefix} ring wait timeout "
                    f"(free={self._ring_free}, want={want_bytes}) — retrying",
                )
                self._estimate_ring_drain(0.2)
                wait_start = now
                last = now

            time.sleep(0.01)

    def append_pcm(self, pcm: bytes, *, stream_end: bool) -> bool:
        if not pcm:
            if not stream_end:
                return True
            return self._post_chunk(b"", stream_end=True)

        total = len(pcm)
        sent = 0
        ok = True

        while sent < total:
            want = min(self._chunk_bytes, total - sent)

            if self._ring_used >= HIGH_WATER_BYTES:
                time.sleep(want / self._bytes_per_sec * 0.35)

            send_len = self._wait_for_send_space(want)
            if send_len < 2:
                ok = False
                break

            piece = pcm[sent : sent + send_len]
            is_last = sent + send_len >= total
            end_stream = bool(stream_end and is_last)
            if not self._post_chunk(piece, stream_end=end_stream):
                ok = False
                self._reset_ring_stats()
                break

            sent += send_len

        return ok

    def _post_chunk(self, piece: bytes, *, stream_end: bool) -> bool:
        headers = dict(self._headers)
        headers["X-Target-Device-Uid"] = self._device_uid
        headers["X-Request-Id"] = f"{self._log_prefix.strip('[]')}_{self._chunk_idx}"
        headers["X-Command-Id"] = f"chunk_{self._chunk_idx}"
        headers["X-Sample-Rate"] = str(self._sample_rate_hz)
        headers["X-Channels"] = str(self._channels)
        headers["X-Stream-End"] = "1" if stream_end else "0"

        if piece and self._binary_play:
            headers["Content-Type"] = "application/octet-stream"
            body: bytes | dict[str, Any] = piece
        else:
            body = {
                "v": 1,
                "target_device_uid": self._device_uid,
                "request_id": headers["X-Request-Id"],
                "command_id": headers["X-Command-Id"],
                "sample_rate_hz": self._sample_rate_hz,
                "channels": self._channels,
                "pcm_b64": base64.b64encode(piece).decode("ascii") if piece else "",
                "stream_end": stream_end,
            }

        for attempt in range(self._max_retries):
            t0 = time.monotonic()
            try:
                if isinstance(body, bytes):
                    resp = self._session.post(
                        self._url,
                        data=body,
                        headers=headers,
                        timeout=self._request_timeout_s,
                    )
                else:
                    resp = self._session.post(
                        self._url,
                        json=body,
                        headers=headers,
                        timeout=self._request_timeout_s,
                    )
            except requests.RequestException as exc:
                wait = min(self._retry_sleep_s * (attempt + 1), 1.0)
                blog(
                    f"{self._log_prefix} chunk {self._chunk_idx} error: {exc} "
                    f"(retry in {wait:.1f}s)",
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
                blog(msg)
                if elapsed_ms > SLOW_CHUNK_MS:
                    blog(
                        f"{self._log_prefix} WARN chunk {self._chunk_idx} slow "
                        f"({elapsed_ms:.0f} ms > {SLOW_CHUNK_MS} ms) — playback may stutter",
                    )
                self._chunk_idx += 1
                return True

            if resp.status_code == 400 and isinstance(body, bytes) and self._binary_play:
                self._binary_play = False
                blog(
                    f"{self._log_prefix} binary /play not supported — falling back to JSON",
                )
                return self._post_chunk(piece, stream_end=stream_end)

            if resp.status_code == 503:
                self._ring_free = 0
                self._ring_used = self._ring_capacity
                time.sleep(min(self._retry_sleep_s * (attempt + 1), 1.0))
                continue

            blog(
                f"{self._log_prefix} chunk {self._chunk_idx} HTTP {resp.status_code} "
                f"({len(piece)} bytes, {elapsed_ms:.0f} ms)",
            )
            return False

        blog(
            f"{self._log_prefix} chunk {self._chunk_idx}: playback stalled after retries",
        )
        self._reset_ring_stats()
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
        blog(f"{log_prefix} requests not installed; skip playback")
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
