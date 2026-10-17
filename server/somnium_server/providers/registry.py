from __future__ import annotations

from typing import Any

from somnium_server.providers.context_stub import StubContextProvider
from somnium_server.providers.image_echo import EchoImageProvider
from somnium_server.providers.image_http import HttpImageProvider
from somnium_server.providers.prompt_openai import OpenAICompatPromptProvider
from somnium_server.providers.prompt_template import (
    EchoPromptProvider,
    TemplatePromptProvider,
)


def context_provider(name: str):
    name = (name or "stub").lower()
    if name == "stub":
        return StubContextProvider()
    raise KeyError(f"Unknown context provider: {name}")


def prompt_provider(name: str):
    name = (name or "template").lower()
    if name in ("template", "echo"):
        return TemplatePromptProvider() if name == "template" else EchoPromptProvider()
    if name in ("openai_compat", "openai", "chat"):
        return OpenAICompatPromptProvider()
    raise KeyError(f"Unknown prompt provider: {name}")


def image_provider(name: str):
    name = (name or "echo").lower()
    if name == "echo":
        return EchoImageProvider()
    if name == "http":
        return HttpImageProvider()
    raise KeyError(
        f"Unknown image provider: {name}. "
        "Add a module under providers/ and register it here."
    )


def build_stack(cfg: dict[str, Any]):
    return {
        "context": context_provider(cfg.get("context", {}).get("provider", "stub")),
        "prompt": prompt_provider(cfg.get("prompt", {}).get("provider", "template")),
        "image": image_provider(cfg.get("image", {}).get("provider", "echo")),
    }
