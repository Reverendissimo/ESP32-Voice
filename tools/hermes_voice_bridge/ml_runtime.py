"""Runtime configuration for ML imports (warnings, HF auth)."""

from __future__ import annotations

import logging
import os
import sys
import warnings
from contextlib import contextmanager
from io import TextIOBase


def configure_ml_runtime(*, hf_token: str = "") -> None:
    """Apply env and warning filters before loading Chatterbox / Hugging Face."""
    token = hf_token.strip() or os.environ.get("HF_TOKEN", "").strip()
    if token:
        os.environ["HF_TOKEN"] = token
        os.environ["HUGGING_FACE_HUB_TOKEN"] = token

    warnings.filterwarnings(
        "ignore",
        message=".*LoRACompatibleLinear.*",
        category=FutureWarning,
    )
    warnings.filterwarnings(
        "ignore",
        message=".*torch.backends.cuda.sdp_kernel.*",
        category=FutureWarning,
    )
    warnings.filterwarnings(
        "ignore",
        message=".*pkg_resources is deprecated.*",
        category=UserWarning,
    )
    warnings.filterwarnings(
        "ignore",
        message=".*unauthenticated requests to the HF Hub.*",
        category=UserWarning,
    )

    logging.getLogger("huggingface_hub").setLevel(logging.ERROR)
    logging.getLogger("diffusers").setLevel(logging.ERROR)
    logging.getLogger("transformers.integrations.sdpa_attention").setLevel(logging.ERROR)


def configure_chatterbox_attention(model: object, *, mode: str = "sdpa") -> None:
    """Set transformer attention implementation (sdpa is faster on CUDA)."""
    t3 = getattr(model, "t3", None)
    tfmr = getattr(t3, "tfmr", None) if t3 is not None else None
    config = getattr(tfmr, "config", None) if tfmr is not None else None
    if config is None:
        return
    impl = "eager" if mode.strip().lower() == "eager" else "sdpa"
    if getattr(config, "_attn_implementation", None) in (None, "sdpa", "eager"):
        config._attn_implementation = impl


class _FilteredStderr(TextIOBase):
    def __init__(self, stream: TextIOBase) -> None:
        self._stream = stream

    def write(self, s: str) -> int:
        if "unauthenticated requests to the HF Hub" in s:
            return len(s)
        return self._stream.write(s)

    def flush(self) -> None:
        self._stream.flush()

    def __getattr__(self, name: str):
        return getattr(self._stream, name)


@contextmanager
def quiet_hf_hub_noise():
    """Hide HF Hub auth chatter printed directly to stderr during model load."""
    old_stderr = sys.stderr
    sys.stderr = _FilteredStderr(old_stderr)
    try:
        yield
    finally:
        sys.stderr = old_stderr
