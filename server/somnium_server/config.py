from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent
CREATIVE_PATH = REPO / "creative" / "creative.json"
SETTINGS_PATH = ROOT / "settings.json"
ENV_PATH = REPO / ".env"


def load_dotenv(path: Path = ENV_PATH) -> None:
    """Load KEY=VALUE lines from .env into os.environ (does not override)."""
    if not path.is_file():
        return
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip().strip("'").strip('"')
        if key and key not in os.environ:
            os.environ[key] = value
    # Convenience aliases
    if os.environ.get("XAI_KEY") and not os.environ.get("XAI_API_KEY"):
        os.environ["XAI_API_KEY"] = os.environ["XAI_KEY"]
    if os.environ.get("XAI_API_KEY") and not os.environ.get("XAI_KEY"):
        os.environ["XAI_KEY"] = os.environ["XAI_API_KEY"]


def env_secret(name: str) -> str | None:
    if not name:
        return None
    value = os.environ.get(name, "").strip()
    return value or None


def _load_json(path: Path) -> dict[str, Any]:
    with path.open() as fh:
        return json.load(fh)


def load_config() -> dict[str, Any]:
    """Merge creative.json (edit this!) with settings.json (plumbing)."""
    load_dotenv()
    creative = _load_json(CREATIVE_PATH)
    settings = _load_json(SETTINGS_PATH) if SETTINGS_PATH.is_file() else {}

    modes = {m["id"]: m for m in creative.get("modes", [])}
    models = creative.get("models") or {}
    venue = settings.get("venue") or {}
    prompt_api = settings.get("prompt_api") or {}
    image_api = settings.get("image_api") or {}
    context_cfg = settings.get("context") or {}

    return {
        "_creative_path": str(CREATIVE_PATH),
        "_settings_path": str(SETTINGS_PATH),
        "server": {
            "host": settings.get("host", "0.0.0.0"),
            "port": settings.get("port", 8080),
            "output_dir": settings.get("output_dir", "outputs"),
        },
        "location": {
            "default_lat": venue.get("lat", 0),
            "default_lon": venue.get("lon", 0),
            "default_label": venue.get("label", "Somnium"),
        },
        "styles": {
            mid: {
                "label": m.get("label", mid),
                "keywords": m.get("keywords", ""),
                "prompt": m.get("prompt", ""),
                "real": bool(m.get("real", False)),
            }
            for mid, m in modes.items()
        },
        "prompt": {
            "provider": models.get("prompt_provider", "template"),
            "model": prompt_api.get("model", "grok-3-mini"),
            "base_url": prompt_api.get("base_url", "https://api.x.ai/v1"),
            "api_key_env": prompt_api.get("api_key_env", "XAI_KEY"),
            "temperature": prompt_api.get("temperature", 0.7),
            "system": creative.get("prompt_system", ""),
            "user_template": (
                "Filter style: {style_label} ({style_keywords}).\n"
                "Grade mood only (do not invent scenery): {context_summary}\n"
                "Captured: {taken_at} near {location_label}.\n"
                "Draft filter instruction: {prompt}\n"
                "Rewrite as a SHORT filter prompt. Preserve the photo; grade only."
            ),
        },
        "image": {
            "provider": models.get("image_provider", "echo"),
            "api_key_env": image_api.get("api_key_env", "XAI_KEY"),
            "base_url": image_api.get("base_url", "https://api.x.ai/v1"),
            "model": image_api.get("model", "grok-imagine-image-2.0"),
            "endpoint": image_api.get("endpoint", ""),
            "method": "POST",
            "body_mode": image_api.get("body_mode", "json"),
            "image_field": image_api.get("image_field", "image"),
            "prompt_field": image_api.get("prompt_field", "prompt"),
            "extra": {
                "model": image_api.get("model", "grok-imagine-image-2.0"),
                **(image_api.get("extra") or {}),
            },
        },
        "context": {
            "provider": models.get("context_provider", "openmeteo"),
            "include_weather": context_cfg.get("include_weather", True),
            "include_astro": context_cfg.get("include_astro", True),
            "include_sun": context_cfg.get("include_sun", True),
        },
        "creative": creative,
    }
