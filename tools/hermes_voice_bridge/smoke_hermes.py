#!/usr/bin/env python3
"""Smoke-test Hermes /v1/responses API."""

from __future__ import annotations

import argparse
import json
import sys

from hermes_client import HermesClient, HermesError


def run_session_test(client: HermesClient, conversation_id: str) -> int:
    print(f"POST {client.endpoint}")
    print(f"conversation={conversation_id}")

    turn1 = client.chat(
        "Hello, my name is Sergio. Please remember that for this conversation.",
        conversation_id=conversation_id,
    )
    print(f"turn 1: {turn1}")

    turn2 = client.chat("What is my name?", conversation_id=conversation_id)
    print(f"turn 2: {turn2}")

    if "sergio" not in turn2.lower():
        print("session continuity check failed: expected name in follow-up reply", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Test Hermes /v1/responses")
    parser.add_argument("prompt", nargs="?", default="Say hello in one short sentence.")
    parser.add_argument("--host", default="192.168.100.25")
    parser.add_argument("--port", type=int, default=8642)
    parser.add_argument("--api-key", default="hermes-local-dev-key-2026")
    parser.add_argument("--model", default="hermes-agent")
    parser.add_argument("--conversation", default="", help="Hermes conversation id")
    parser.add_argument(
        "--session-test",
        action="store_true",
        help="Run two-turn conversation continuity test (Sergio name recall)",
    )
    parser.add_argument("--raw", action="store_true", help="Print full JSON response (non-stream)")
    args = parser.parse_args()

    client = HermesClient(
        host=args.host,
        port=args.port,
        api_key=args.api_key,
        model=args.model,
        stream=not args.raw,
    )

    conversation_id = args.conversation or "voice-box-smoke-test"

    try:
        if args.session_test:
            return run_session_test(client, conversation_id)

        print(f"POST {client.endpoint}")
        print(f"conversation={conversation_id}")

        if args.raw:
            import requests

            headers, body = client._build_request(args.prompt, conversation_id=conversation_id)
            body["stream"] = False
            response = requests.post(
                client.endpoint,
                headers=headers,
                json=body,
                timeout=(client._connect_timeout_s, client.read_timeout_s),
            )
            response.raise_for_status()
            print(json.dumps(response.json(), indent=2))
        else:
            reply = client.chat(args.prompt, conversation_id=conversation_id)
            print(reply)
    except HermesError as exc:
        print(f"failed ({exc.kind}): {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
