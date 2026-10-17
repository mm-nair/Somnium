from __future__ import annotations

from typing import Any, Protocol


class PromptProvider(Protocol):
    def synthesize(self, *, style_id: str, style: dict[str, Any],
                   context: dict[str, Any], prompt_cfg: dict[str, Any]) -> str: ...


class ImageProvider(Protocol):
    def transform(self, *, image: bytes, prompt: str,
                  image_cfg: dict[str, Any]) -> bytes: ...


class ContextProvider(Protocol):
    def enrich(self, *, taken_at: str | None, client_ip: str | None,
               lat: float | None, lon: float | None,
               location_cfg: dict[str, Any],
               context_cfg: dict[str, Any]) -> dict[str, Any]: ...
