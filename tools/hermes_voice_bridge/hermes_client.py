"""Client for Hermes agent /v1/responses API."""

from __future__ import annotations

import json
import os
import re
import threading
import time
from collections.abc import Callable
from typing import Any

try:
    import requests
    from requests.exceptions import ConnectionError as RequestsConnectionError
    from requests.exceptions import HTTPError, RequestException, Timeout
except ImportError:
    requests = None  # type: ignore[assignment]
    RequestsConnectionError = ConnectionError  # type: ignore[misc, assignment]
    HTTPError = RequestException = Timeout = Exception  # type: ignore[misc, assignment]


DEFAULT_VOICE_INSTRUCTIONS = (
    "You are speaking through a voice terminal. Keep answers short and conversational. "
    "Use 1-3 sentences. Avoid markdown, lists, tables, code. "
    "You may use Chatterbox Turbo tags like [laugh], [cough], [sigh], [chuckle] sparingly "
    "(they are rendered as sounds, not spoken words). "
    "User messages are speech-to-text from a microphone and may be inaccurate. "
    "If a message does not make sense or you are unsure what was asked, "
    "say briefly that you did not understand and ask them to repeat."
)

SKIPPED_SSE_EVENTS = frozenset({"hermes.tool.progress"})
TEXT_SSE_EVENTS = frozenset({"response.output_text.delta"})


class HermesError(Exception):
    """Expected Hermes request failure (timeout, connection drop, bad response)."""

    def __init__(self, message: str, *, kind: str = "unknown") -> None:
        super().__init__(message)
        self.kind = kind


class HermesClient:
    def __init__(
        self,
        *,
        host: str = "192.168.100.25",
        port: int = 8642,
        api_key: str = "hermes-local-dev-key-2026",
        model: str = "hermes-agent",
        voice_instructions: str = DEFAULT_VOICE_INSTRUCTIONS,
        conversation_prefix: str = "voice-box",
        timeout_s: float = 300.0,
        connect_timeout_s: float = 10.0,
        max_retries: int = 1,
        retry_backoff_s: float = 2.0,
        stream: bool = True,
    ) -> None:
        if requests is None:
            raise RuntimeError("requests is not installed (pip install requests)")

        self._host = host.strip() or "192.168.100.25"
        self._port = int(port)
        self._api_key = api_key.strip()
        self._model = model.strip() or "hermes-agent"
        self._voice_instructions = voice_instructions.strip()
        self._conversation_prefix = re.sub(
            r"[^a-zA-Z0-9_-]",
            "-",
            (conversation_prefix.strip() or "voice-box"),
        ).strip("-") or "voice-box"
        self._read_timeout_s = max(1.0, float(timeout_s))
        self._connect_timeout_s = max(1.0, float(connect_timeout_s))
        self._max_retries = max(0, int(max_retries))
        self._retry_backoff_s = max(0.0, float(retry_backoff_s))
        self._stream = stream
        self._url = f"http://{self._host}:{self._port}/v1/responses"
        self._introduced_conversations: set[str] = set()
        self._conversation_lock = threading.Lock()

    @property
    def endpoint(self) -> str:
        return self._url

    @property
    def model(self) -> str:
        return self._model

    @property
    def read_timeout_s(self) -> float:
        return self._read_timeout_s

    @staticmethod
    def _safe_id(value: str, *, max_len: int = 48) -> str:
        cleaned = re.sub(r"[^a-zA-Z0-9_-]", "-", value.strip()).strip("-")
        if not cleaned:
            return "box"
        return cleaned[:max_len]

    def conversation_for_device(self, device_uid: str, session_id: str = "") -> str:
        """Deprecated: use a shared server session id instead."""
        uid = self._safe_id(device_uid, max_len=32)
        return f"{self._conversation_prefix}-{uid}"

    def mark_conversation_introduced(self, conversation_id: str) -> None:
        conversation = self._safe_id(conversation_id, max_len=64)
        with self._conversation_lock:
            self._introduced_conversations.add(conversation)

    def _build_request(
        self,
        user_message: str,
        *,
        conversation_id: str,
    ) -> tuple[dict[str, str], dict[str, Any]]:
        conversation = self._safe_id(conversation_id, max_len=64)
        body: dict[str, Any] = {
            "model": self._model,
            "conversation": conversation,
            "input": user_message.strip(),
            "stream": self._stream,
        }

        with self._conversation_lock:
            first_turn = conversation not in self._introduced_conversations
            if first_turn and self._voice_instructions:
                body["instructions"] = self._voice_instructions
                self._introduced_conversations.add(conversation)

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self._api_key}",
        }
        return headers, body

    def _request(self, body: dict[str, Any], headers: dict[str, str]) -> dict[str, Any]:
        last_error: Exception | None = None
        attempts = self._max_retries + 1

        for attempt in range(1, attempts + 1):
            started = time.monotonic()
            try:
                response = requests.post(
                    self._url,
                    headers=headers,
                    json=body,
                    timeout=(self._connect_timeout_s, self._read_timeout_s),
                )
                response.raise_for_status()
                return response.json()
            except Timeout as exc:
                elapsed = time.monotonic() - started
                raise HermesError(
                    f"no response within {self._read_timeout_s:.0f}s (after {elapsed:.1f}s)",
                    kind="timeout",
                ) from exc
            except HTTPError as exc:
                status = exc.response.status_code if exc.response is not None else 0
                raise HermesError(
                    f"HTTP {status} from Hermes",
                    kind="http",
                ) from exc
            except RequestsConnectionError as exc:
                last_error = exc
                if attempt < attempts:
                    time.sleep(self._retry_backoff_s)
                    continue
                raise HermesError(
                    "connection lost before Hermes replied",
                    kind="connection",
                ) from exc
            except RequestException as exc:
                raise HermesError(
                    f"request failed: {exc}",
                    kind="connection",
                ) from exc

        raise HermesError(
            "connection lost before Hermes replied",
            kind="connection",
        ) from last_error

    def _text_from_response_payload(self, payload: dict[str, Any]) -> str:
        typ = str(payload.get("type", ""))
        if typ == "response.output_text.delta":
            return str(payload.get("delta") or "")

        try:
            choice = payload["choices"][0]
            delta = choice.get("delta", {}).get("content", "")
            if not delta:
                delta = choice.get("message", {}).get("content", "")
            return str(delta) if delta else ""
        except (KeyError, IndexError, TypeError):
            return ""

    def _text_from_response_object(self, payload: dict[str, Any]) -> str:
        parts: list[str] = []
        for item in payload.get("output", []):
            if item.get("type") != "message":
                continue
            for block in item.get("content", []):
                if block.get("type") == "output_text":
                    text = str(block.get("text", "")).strip()
                    if text:
                        parts.append(text)
        return "\n".join(parts).strip()

    def _request_stream(
        self,
        body: dict[str, Any],
        headers: dict[str, str],
        *,
        on_delta: Callable[[str], None] | None = None,
    ) -> str:
        started = time.monotonic()
        try:
            response = requests.post(
                self._url,
                headers=headers,
                json=body,
                timeout=(self._connect_timeout_s, self._read_timeout_s),
                stream=True,
            )
            response.raise_for_status()
        except Timeout as exc:
            elapsed = time.monotonic() - started
            raise HermesError(
                f"no response within {self._read_timeout_s:.0f}s (after {elapsed:.1f}s)",
                kind="timeout",
            ) from exc
        except HTTPError as exc:
            status = exc.response.status_code if exc.response is not None else 0
            raise HermesError(f"HTTP {status} from Hermes", kind="http") from exc
        except RequestsConnectionError as exc:
            raise HermesError("connection lost before Hermes replied", kind="connection") from exc
        except RequestException as exc:
            raise HermesError(f"request failed: {exc}", kind="connection") from exc

        parts: list[str] = []
        assembled = ""
        pending_event: str | None = None
        for raw_line in response.iter_lines(decode_unicode=True):
            if raw_line is None:
                continue
            line = raw_line.strip()
            if not line:
                pending_event = None
                continue
            if line.startswith("event:"):
                pending_event = line[6:].strip()
                continue
            if not line.startswith("data:"):
                continue
            if pending_event in SKIPPED_SSE_EVENTS:
                continue
            if pending_event not in TEXT_SSE_EVENTS:
                continue

            data = line[5:].strip()
            if data == "[DONE]":
                break

            try:
                payload = json.loads(data)
            except json.JSONDecodeError:
                continue

            delta = self._text_from_response_payload(payload)
            if not delta:
                continue

            if delta.startswith(assembled):
                incremental = delta[len(assembled) :]
                assembled = delta
            else:
                incremental = delta
                assembled += delta

            if not incremental:
                continue

            parts.append(incremental)
            if on_delta is not None:
                on_delta(incremental)

        reply = assembled.strip() or "".join(parts).strip()
        if not reply:
            raise HermesError("empty streamed reply", kind="response")
        return reply

    def chat(
        self,
        user_message: str,
        *,
        conversation_id: str = "",
        session_id: str = "",
        on_delta: Callable[[str], None] | None = None,
    ) -> str:
        cleaned = user_message.strip()
        if not cleaned:
            return ""

        convo = conversation_id.strip() or session_id.strip()
        if not convo:
            convo = "voice-box-default"

        headers, body = self._build_request(cleaned, conversation_id=convo)
        if self._stream:
            return self._request_stream(body, headers, on_delta=on_delta)

        payload = self._request(body, headers)
        reply = self._text_from_response_object(payload)
        if reply:
            return reply
        raise HermesError(f"unexpected Hermes response: {payload!r}", kind="response")

    def ping(self) -> str:
        return self.chat("Reply with exactly: pong", conversation_id=f"{self._conversation_prefix}-ping")


def build_hermes_from_args(args) -> HermesClient | None:
    if getattr(args, "no_hermes", False):
        return None
    instructions = (
        getattr(args, "hermes_voice_instructions", "")
        or getattr(args, "hermes_system_prompt", "")
        or os.environ.get("HERMES_VOICE_INSTRUCTIONS", "")
        or DEFAULT_VOICE_INSTRUCTIONS
    )
    return HermesClient(
        host=os.environ.get("HERMES_HOST", getattr(args, "hermes_host", "192.168.100.25")),
        port=int(os.environ.get("HERMES_PORT", getattr(args, "hermes_port", 8642))),
        api_key=getattr(args, "hermes_api_key", "") or os.environ.get("HERMES_API_KEY", "hermes-local-dev-key-2026"),
        model=os.environ.get("HERMES_MODEL", getattr(args, "hermes_model", "hermes-agent")),
        voice_instructions=instructions,
        conversation_prefix=getattr(args, "hermes_conversation_prefix", "voice-box"),
        timeout_s=getattr(args, "hermes_timeout_s", 300.0),
        connect_timeout_s=getattr(args, "hermes_connect_timeout_s", 10.0),
        max_retries=getattr(args, "hermes_max_retries", 1),
        stream=not getattr(args, "no_hermes_stream", False),
    )
