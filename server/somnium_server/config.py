from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent
CREATIVE_PATH = REPO / "creative" / "creative.json"
SETTINGS_PATH = ROOT / "settings.json"


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
    creative = _load_json(CREATIVE_PATH)
    settings = _load_json(SETTINGS_PATH) if SETTINGS_PATH.is_file() else {}

    modes = {m["id"]: m for m in creative.get("modes", [])}
    models = creative.get("models") or {}
    venue = settings.get("venue") or {}
    prompt_api = settings.get("prompt_api") or {}
    image_api = settings.get("image_api") or {}

    # Shape expected by existing pipeline / providers.
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
            "model": prompt_api.get("model", "gpt-4o-mini"),
            "base_url": prompt_api.get("base_url", "https://api.openai.com/v1"),
            "api_key_env": prompt_api.get("api_key_env", "SOMNIUM_PROMPT_API_KEY"),
            "temperature": 0.7,
            "system": creative.get("prompt_system", ""),
            "user_template": "{prompt}",
        },
        "image": {
            "provider": models.get("image_provider", "echo"),
            "api_key_env": image_api.get("api_key_env", "SOMNIUM_IMAGE_API_KEY"),
            "endpoint": image_api.get("endpoint", ""),
            "method": "POST",
            "body_mode": image_api.get("body_mode", "json"),
            "image_field": image_api.get("image_field", "image"),
            "prompt_field": image_api.get("prompt_field", "prompt"),
            "extra": image_api.get("extra") or {},
        },
        "context": {
            "provider": "stub",
            "include_weather": True,
            "include_astro": True,
            "include_sun": True,
        },
        "creative": creative,
    }
