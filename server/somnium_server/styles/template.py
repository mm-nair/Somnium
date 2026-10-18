"""Fill a style prompt from cook context.

`{context}` is the lighting finish (mood artifacts joined, or a fallback when
there are none). That matches the drafts in creative.json. Place, weather, and
day part are available for user styles that opt in. Built-in modes do not use
`{place}` or `{moon}`, because naming a venue or a sky object makes the image
model paint it into the frame.
"""

from __future__ import annotations

from typing import Any

from somnium_server.styles.schema import ALLOWED_VARIABLES


def template_variables(
    *,
    style_id: str,
    style: dict[str, Any],
    context: dict[str, Any],
) -> dict[str, Any]:
    lighting = "; ".join(context.get("artifacts") or []) or "retain the existing lighting"
    weather = context.get("weather") or {}
    astro = context.get("astro") or {}
    variation = context.get("variation") or {}
    if isinstance(variation, dict):
        variation_text = ". ".join(str(value) for value in variation.values() if value)
    else:
        variation_text = str(variation or "")
    temp = weather.get("temp_c") if isinstance(weather, dict) else None
    wind = weather.get("wind_ms") if isinstance(weather, dict) else None
    place = context.get("location_label", "")
    return {
        "style_id": style_id,
        "style_label": style.get("label", style_id),
        "style_keywords": style.get("keywords", ""),
        "context_summary": lighting,
        "context": lighting,
        "taken_at": context.get("taken_at", ""),
        "location_label": place,
        "place": place if place is not None else "",
        "weather": (weather.get("summary") if isinstance(weather, dict) else "") or "",
        "day_part": context.get("day_part") or "",
        "temp_c": "" if temp is None else temp,
        "wind": "" if wind is None else wind,
        "moon": (astro.get("moon_phase") if isinstance(astro, dict) else "") or "",
        "prompt": style.get("prompt", ""),
        "variation": variation_text,
        "negative": style.get("negative") or "",
        "lat": context.get("lat", ""),
        "lon": context.get("lon", ""),
    }


def render_template(template: str, variables: dict[str, Any]) -> str:
    """One-pass format. Unknown names raise KeyError, same as str.format."""
    missing = [name for name in variables if name not in ALLOWED_VARIABLES]
    if missing:
        raise KeyError(missing[0])

    class _Map(dict):
        def __missing__(self, key: str) -> str:
            raise KeyError(key)

    return str(template).format_map(_Map(variables)).strip()
