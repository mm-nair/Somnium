from __future__ import annotations

from typing import Any


class EchoImageProvider:
    """Dev provider: returns the input JPEG unchanged."""

    def transform(self, *, image: bytes, prompt: str, image_cfg: dict[str, Any]) -> bytes:
        _ = prompt, image_cfg
        return image
