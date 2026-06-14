#!/usr/bin/env python3
"""Trigger ESP32 OTA update via device REST API."""

from __future__ import annotations

import argparse
import json
import sys

try:
    import requests
except ImportError:
    print("pip install requests", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Trigger ESP32-Voice OTA update")
    parser.add_argument("device_ip", help="BOX IP address")
    parser.add_argument("--ota-secret", required=True, help="config.ota.secret value")
    parser.add_argument("--url", default="", help="Optional direct firmware URL (skip manifest)")
    parser.add_argument("--force", action="store_true", help="Update even if version matches")
    parser.add_argument("--status", action="store_true", help="Only query OTA status")
    args = parser.parse_args()

    headers = {"X-Ota-Secret": args.ota_secret, "Content-Type": "application/json"}
    base = f"http://{args.device_ip}/api/v1/ota"

    if args.status:
        resp = requests.get(f"{base}/status", headers=headers, timeout=10)
        print(resp.status_code, resp.text)
        return 0 if resp.status_code == 200 else 1

    body: dict = {}
    if args.url:
        body["url"] = args.url
    if args.force:
        body["force"] = True

    resp = requests.post(f"{base}/update", headers=headers, json=body, timeout=15)
    print(resp.status_code, resp.text)
    return 0 if resp.status_code in (200, 202) else 1


if __name__ == "__main__":
    raise SystemExit(main())
