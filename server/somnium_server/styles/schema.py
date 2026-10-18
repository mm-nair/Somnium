"""Validate a style document before it enters the registry.

Built-in modes in ``creative/creative.json`` use the same fields. User JSON
and plugins are checked strictly so a typo does not silently change a cook.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any


class StyleError(ValueError):
    def __init__(self, errors: list[str]):
        self.errors = errors
        super().__init__("; ".join(errors))


_ID = re.compile(r"^[a-z][a-z0-9_-]{0,31}$")
_PLACEHOLDER = re.compile(r"\{([a-zA-Z_][a-zA-Z0-9_]*)\}")

# Names a prompt template may use. `{context}` stays the lighting finish the
# built-in modes already rely on (mood artifacts, not the raw sky summary).
ALLOWED_VARIABLES = frozenset(
    {
        "context",
        "context_summary",
        "variation",
        "style_id",
        "style_label",
        "style_keywords",
        "place",
        "location_label",
        "weather",
        "day_part",
        "temp_c",
        "wind",
        "moon",
        "taken_at",
        "negative",
        "prompt",
        "lat",
        "lon",
    }
)

_KNOWN_KEYS = frozenset(
    {
        "id",
        "label",
        "keywords",
        "prompt",
        "real",
        "liberal",
        "variations",
        "preview",
        "tint",
        "negative",
        "tuning",
        "enabled",
    }
)
_TUNING_KEYS = frozenset(
    {"strength", "temperature", "prompt_model", "image_model", "negative", "top_p"}
)


@dataclass
class Style:
    id: str
    label: str
    keywords: str = ""
    prompt: str = ""
    real: bool = False
    liberal: bool = False
    variations: dict[str, list[str]] = field(default_factory=dict)
    preview: str = ""
    tint: list[int] | None = None
    negative: str = ""
    tuning: dict[str, Any] = field(default_factory=dict)
    enabled: bool = True
    source: str = ""

    def as_pipeline_dict(self) -> dict[str, Any]:
        return {
            "label": self.label,
            "keywords": self.keywords,
            "prompt": self.prompt,
            "real": self.real,
            "liberal": self.liberal,
            "variations": self.variations,
            "preview": self.preview,
            "tint": self.tint,
            "negative": self.negative,
            "tuning": dict(self.tuning),
            "enabled": self.enabled,
            "source": self.source,
        }

    def public_dict(self) -> dict[str, Any]:
        data = self.as_pipeline_dict()
        data["id"] = self.id
        data["variables"] = sorted(placeholders(self.prompt))
        return data


def placeholders(template: str) -> set[str]:
    return set(_PLACEHOLDER.findall(template or ""))


def validate_style(raw: Any, *, source: str, strict: bool = True) -> Style:
    errors: list[str] = []
    if not isinstance(raw, dict):
        raise StyleError([f"{source}: style must be a JSON object"])

    if strict:
        unknown = sorted(set(raw) - _KNOWN_KEYS)
        if unknown:
            errors.append(f"unknown fields: {', '.join(unknown)}")

    style_id = raw.get("id")
    if not isinstance(style_id, str) or not _ID.fullmatch(style_id):
        errors.append("id must match ^[a-z][a-z0-9_-]{0,31}$")
        style_id = "invalid"

    label = raw.get("label", style_id)
    if not isinstance(label, str) or not label.strip() or len(label) > 48:
        errors.append("label must be a non-empty string up to 48 characters")
        label = str(style_id)

    keywords = raw.get("keywords", "")
    if not isinstance(keywords, str) or len(keywords) > 500:
        errors.append("keywords must be a string up to 500 characters")
        keywords = ""

    prompt = raw.get("prompt", "")
    if not isinstance(prompt, str) or len(prompt) > 8000:
        errors.append("prompt must be a string up to 8000 characters")
        prompt = ""
    else:
        unknown_vars = sorted(placeholders(prompt) - ALLOWED_VARIABLES)
        if unknown_vars:
            errors.append(
                "prompt uses unknown variables: " + ", ".join(unknown_vars)
            )
        stripped = _PLACEHOLDER.sub("", prompt)
        if "{" in stripped or "}" in stripped:
            errors.append("prompt has unmatched braces")

    real = _bool_field(raw, "real", default=False, errors=errors)
    liberal = _bool_field(raw, "liberal", default=False, errors=errors)
    enabled = _bool_field(raw, "enabled", default=True, errors=errors)

    variations = _variations(raw.get("variations", {}), errors)
    preview = _preview(raw.get("preview", ""), errors)
    tint = _tint(raw.get("tint", None), errors)
    negative = _short_text(raw.get("negative", ""), "negative", 500, errors)
    tuning = _tuning(raw.get("tuning", {}), errors, strict=strict)
    if tuning.get("negative") and not negative:
        negative = str(tuning["negative"])

    if errors:
        where = style_id if isinstance(raw.get("id"), str) else source
        raise StyleError([f"{where}: {item}" for item in errors])

    return Style(
        id=style_id,
        label=label.strip(),
        keywords=keywords,
        prompt=prompt,
        real=real,
        liberal=liberal,
        variations=variations,
        preview=preview,
        tint=tint,
        negative=negative,
        tuning=tuning,
        enabled=enabled,
        source=source,
    )


def _bool_field(raw: dict[str, Any], key: str, *, default: bool, errors: list[str]) -> bool:
    if key not in raw:
        return default
    value = raw[key]
    if not isinstance(value, bool):
        errors.append(f"{key} must be true or false")
        return default
    return value


def _variations(value: Any, errors: list[str]) -> dict[str, list[str]]:
    if value in (None, {}):
        return {}
    if not isinstance(value, dict):
        errors.append("variations must be an object of string lists")
        return {}
    out: dict[str, list[str]] = {}
    for category, options in value.items():
        if not isinstance(category, str) or not category.strip() or len(category) > 40:
            errors.append("variation category names must be short strings")
            continue
        if not isinstance(options, list) or not options:
            errors.append(f"variations.{category} must be a non-empty list")
            continue
        cleaned: list[str] = []
        for option in options:
            if not isinstance(option, str) or not option.strip() or len(option) > 500:
                errors.append(f"variations.{category} has an invalid option")
                continue
            cleaned.append(option)
        if cleaned:
            out[category] = cleaned
    return out


def _preview(value: Any, errors: list[str]) -> str:
    if value in (None, ""):
        return ""
    if not isinstance(value, str) or len(value) > 200:
        errors.append("preview must be a short relative path")
        return ""
    if value.startswith(("/", "\\")) or ".." in value.replace("\\", "/").split("/"):
        errors.append("preview must stay inside the repo (no absolute path or ..)")
        return ""
    return value


def _tint(value: Any, errors: list[str]) -> list[int] | None:
    if value is None:
        return None
    if (
        not isinstance(value, list)
        or len(value) != 3
        or any(not isinstance(channel, int) or isinstance(channel, bool) for channel in value)
        or any(channel < 0 or channel > 255 for channel in value)
    ):
        errors.append("tint must be three integers from 0 to 255")
        return None
    return [int(channel) for channel in value]


def _short_text(value: Any, name: str, limit: int, errors: list[str]) -> str:
    if value in (None, ""):
        return ""
    if not isinstance(value, str) or len(value) > limit:
        errors.append(f"{name} must be a string up to {limit} characters")
        return ""
    return value


def _tuning(value: Any, errors: list[str], *, strict: bool) -> dict[str, Any]:
    if value in (None, {}):
        return {}
    if not isinstance(value, dict):
        errors.append("tuning must be an object")
        return {}
    if strict:
        unknown = sorted(set(value) - _TUNING_KEYS)
        if unknown:
            errors.append("unknown tuning fields: " + ", ".join(unknown))
    out: dict[str, Any] = {}
    if "strength" in value:
        strength = _unit_float(value["strength"], "tuning.strength", 0.0, 1.0, errors)
        if strength is not None:
            out["strength"] = strength
    if "temperature" in value:
        temperature = _unit_float(
            value["temperature"], "tuning.temperature", 0.0, 2.0, errors
        )
        if temperature is not None:
            out["temperature"] = temperature
    if "top_p" in value:
        top_p = _unit_float(value["top_p"], "tuning.top_p", 0.0, 1.0, errors)
        if top_p is not None:
            out["top_p"] = top_p
    for key, limit in (("prompt_model", 80), ("image_model", 80), ("negative", 500)):
        if key not in value:
            continue
        text = value[key]
        if not isinstance(text, str) or not text.strip() or len(text) > limit:
            errors.append(f"tuning.{key} must be a non-empty string up to {limit} characters")
            continue
        out[key] = text.strip()
    return out


def _unit_float(
    value: Any, name: str, low: float, high: float, errors: list[str]
) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        errors.append(f"{name} must be a number from {low:g} to {high:g}")
        return None
    number = float(value)
    if number < low or number > high:
        errors.append(f"{name} must be from {low:g} to {high:g}")
        return None
    return number
