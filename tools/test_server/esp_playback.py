"""Paced PCM streaming to ESP32 /api/v1/play with ring-buffer flow control."""

from __future__ import annotations

import base64
import time

try:
    import requests
except ImportError:
    requests = None  # type: ignore[assignment]

RING_CAPACITY_BYTES = 320 * 1024


def _ring_stats(payload: dict) -> tuple[int, int, int]:
    used = int(payload.get("ring_used_bytes", 0))
    free = int(payload.get("ring_free_bytes", RING_CAPACITY_BYTES - used))
    capacity = int(payload.get("ring_capacity_bytes", RING_CAPACITY_BYTES))
    return used, free, capacity


def stream_pcm_to_esp(
    device_ip: str,
    device_uid: str,
    pcm: bytes,
    sample_rate_hz: int,
    channels: int,
    auth_token: str = "",
    *,
    chunk_bytes: int = 8192,
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

    url = f"http://{device_ip}/api/v1/play"
    headers = {"Content-Type": "application/json", "Connection": "close"}
    if auth_token:
        headers["X-Auth-Token"] = auth_token

    if not pcm:
        if not stream_end:
            return True
        body = {
            "v": 1,
            "target_device_uid": device_uid,
            "request_id": f"{log_prefix.strip('[]')}_end",
            "command_id": "stream_end",
            "sample_rate_hz": sample_rate_hz,
            "channels": channels,
            "pcm_b64": "",
            "stream_end": True,
        }
        try:
            resp = requests.post(url, json=body, headers=headers, timeout=request_timeout_s)
            return resp.status_code == 200
        except requests.RequestException:
            return False

    chunk_bytes = max(2, chunk_bytes - (chunk_bytes % 2))
    if low_water_bytes <= 0:
        low_water_bytes = chunk_bytes * 2

    bytes_per_sec = max(1, sample_rate_hz * channels * 2)
    total = len(pcm)
    sent = 0
    chunk_idx = 0
    ring_free = RING_CAPACITY_BYTES
    ring_capacity = RING_CAPACITY_BYTES
    ring_stalled = False
    ok = True

    while sent < total:
        piece = pcm[sent : sent + chunk_bytes]
        needed = len(piece) if ring_stalled else low_water_bytes
        while ring_free < needed:
            time.sleep(0.2 if ring_stalled else 0.05)
            if not ring_stalled:
                ring_free = min(ring_capacity, ring_free + int(bytes_per_sec * 0.05))

        is_last = sent + len(piece) >= total
        end_stream = stream_end if stream_end is not None else is_last
        body = {
            "v": 1,
            "target_device_uid": device_uid,
            "request_id": f"{log_prefix.strip('[]')}_{chunk_idx}",
            "command_id": f"chunk_{chunk_idx}",
            "sample_rate_hz": sample_rate_hz,
            "channels": channels,
            "pcm_b64": base64.b64encode(piece).decode("ascii"),
            "stream_end": bool(end_stream and is_last),
        }

        posted = False
        for attempt in range(max_retries):
            try:
                resp = requests.post(url, json=body, headers=headers, timeout=request_timeout_s)
            except requests.RequestException as exc:
                wait = min(retry_sleep_s * (attempt + 1), 1.0)
                print(
                    f"{log_prefix} chunk {chunk_idx} error: {exc} (retry in {wait:.1f}s)",
                    flush=True,
                )
                time.sleep(wait)
                continue

            if resp.status_code == 200:
                try:
                    payload = resp.json()
                    _, ring_free, ring_capacity = _ring_stats(payload)
                except ValueError:
                    ring_free = max(0, ring_free - len(piece))
                ring_stalled = False
                posted = True
                print(
                    f"{log_prefix} chunk {chunk_idx} ok ({len(piece)} bytes, "
                    f"{sent + len(piece)}/{total}, ring_free={ring_free})",
                    flush=True,
                )
                break

            if resp.status_code == 503:
                ring_stalled = True
                ring_free = 0
                time.sleep(min(retry_sleep_s * (attempt + 1), 1.0))
                continue

            ok = False
            print(
                f"{log_prefix} chunk {chunk_idx} HTTP {resp.status_code} "
                f"({len(piece)} bytes)",
                flush=True,
            )
            break

        if not posted:
            ok = False
            print(f"{log_prefix} chunk {chunk_idx}: playback stalled after retries", flush=True)
            break

        sent += len(piece)
        chunk_idx += 1

    return ok
