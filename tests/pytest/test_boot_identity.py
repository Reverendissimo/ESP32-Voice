"""Boot and identity integration tests (require flashed hardware)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.hardware


def test_placeholder_boot_identity() -> None:
    """Placeholder until pytest-embedded fixtures are wired to a board."""
    pytest.skip("Hardware integration harness not wired yet")
