#!/usr/bin/env python3
"""Smoke-test ESP32 OTA endpoints and trigger update."""

from __future__ import annotations

import argparse
import json
import sys
import time

try:
    import requests
except ImportError:
    print("pip install requests", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke-test ESP32-Voice OTA")
    parser.add_argument("device_ip", default="192.168.100.217", nargs="?")
    parser.add_argument("--ota-secret", required=True)
    parser.add_argument("--server", default="http://192.168.100.70:8080/api/v1")
    parser.add_argument("--trigger", action="store_true", help="POST /ota/update")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    device = f"http://{args.device_ip}"
    headers = {"X-Ota-Secret": args.ota_secret}

    ver = requests.get(f"{device}/api/v1/version", timeout=10)
    print(f"device version: {ver.status_code} {ver.text}")

    manifest = requests.get(f"{args.server.rstrip('/')}/firmware/manifest.json", timeout=10)
    print(f"manifest: {manifest.status_code} {manifest.text}")

    status = requests.get(f"{device}/api/v1/ota/status", headers=headers, timeout=10)
    print(f"ota status: {status.status_code} {status.text}")
    if status.status_code == 404:
        print("OTA routes missing — flash firmware with OTA support first.", file=sys.stderr)
        return 2

    if not args.trigger:
        return 0 if status.status_code == 200 else 1

    body = {"force": True} if args.force else {}
    update = requests.post(f"{device}/api/v1/ota/update", headers=headers, json=body, timeout=15)
    print(f"ota update: {update.status_code} {update.text}")
    if update.status_code not in (200, 202):
        return 1

    for i in range(30):
        time.sleep(2)
        try:
            st = requests.get(f"{device}/api/v1/ota/status", headers=headers, timeout=5)
            print(f"poll {i}: {st.status_code} {st.text}")
            if st.status_code != 200:
                continue
            payload = st.json()
            if payload.get("state") == "rebooting":
                break
        except requests.RequestException as exc:
            print(f"poll {i}: {exc}")

    time.sleep(8)
    ver2 = requests.get(f"{device}/api/v1/version", timeout=15)
    print(f"version after reboot: {ver2.status_code} {ver2.text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
