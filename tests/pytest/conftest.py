"""Shared pytest fixtures for ESP32-Voice board integration tests."""

from __future__ import annotations

import os

import pytest

FIRMWARE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "firmware")
DEFAULT_SERIAL = os.environ.get("ESP32_VOICE_SERIAL", "/dev/ttyACM0")
DEFAULT_API_BASE = os.environ.get("ESP32_VOICE_API_BASE", "http://192.168.4.1/api/v1")


@pytest.fixture(scope="session")
def firmware_dir() -> str:
    """Absolute path to the ESP-IDF firmware project."""
    return os.path.abspath(FIRMWARE_DIR)


@pytest.fixture(scope="session")
def serial_port() -> str:
    """USB serial port for the BOX-3."""
    return DEFAULT_SERIAL


@pytest.fixture(scope="session")
def api_base_url() -> str:
    """Base URL for local REST API during integration tests."""
    return DEFAULT_API_BASE
