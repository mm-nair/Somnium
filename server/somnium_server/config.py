from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG_PATH = ROOT / "config.yaml"
EXAMPLE_CONFIG_PATH = ROOT / "config.example.yaml"


def load_config(path: Path | None = None) -> dict[str, Any]:
    cfg_path = path or DEFAULT_CONFIG_PATH
    if not cfg_path.is_file():
        if EXAMPLE_CONFIG_PATH.is_file() and path is None:
            cfg_path = EXAMPLE_CONFIG_PATH
        else:
            raise FileNotFoundError(
                f"Missing {cfg_path}. Copy config.example.yaml → config.yaml"
            )
    with cfg_path.open() as fh:
        data = yaml.safe_load(fh) or {}
    data["_config_path"] = str(cfg_path)
    return data


def env_secret(name: str) -> str | None:
    if not name:
        return None
    value = os.environ.get(name, "").strip()
    return value or None
