"""Pick one-of-a-kind optical artifacts from live mood context.

The map lives in creative/creative.json → mood_artifacts.
Pipeline injects `context["artifacts"]` + `context["mood_watermark"]` before
the prompt model runs, so weather/night/moon actually reach the final edit prompt.
"""

from __future__ import annotations

import hashlib
import random
from typing import Any


def _rng(seed_bits: str) -> random.Random:
    digest = hashlib.sha256(seed_bits.encode()).hexdigest()
    return random.Random(int(digest[:16], 16))


def _match_tags(ctx: dict[str, Any]) -> list[str]:
    tags: list[str] = []
    day = (ctx.get("day_part") or "").lower()
    if day:
        tags.append(day)
    weather = ((ctx.get("weather") or {}).get("summary") or "").lower()
    for token in (
        "clear",
        "cloud",
        "rain",
        "drizzle",
        "snow",
        "fog",
        "thunder",
        "overcast",
    ):
        if token in weather:
            tags.append(token)
    clouds = (ctx.get("weather") or {}).get("cloud_cover_pct")
    if isinstance(clouds, (int, float)):
        if clouds < 25:
            tags.append("clearish")
        elif clouds > 70:
            tags.append("heavy_cloud")
    temp = (ctx.get("weather") or {}).get("temp_c")
    if isinstance(temp, (int, float)):
        if temp <= 5:
            tags.append("cold")
        elif temp >= 22:
            tags.append("warm")
    moon = ((ctx.get("astro") or {}).get("moon_phase") or "").lower()
    if "full" in moon:
        tags.append("full_moon")
    elif "new" in moon:
        tags.append("new_moon")
    elif moon:
        tags.append("moonlit")
    wind = (ctx.get("weather") or {}).get("wind_ms")
    if isinstance(wind, (int, float)) and wind >= 6:
        tags.append("windy")
    return tags


def apply_mood_artifacts(
    context: dict[str, Any],
    *,
    style_id: str,
    creative: dict[str, Any],
) -> dict[str, Any]:
    """Mutate/return context with artifacts + a short mood_watermark string."""
    cfg = creative.get("mood_artifacts") or {}
    pools: dict[str, list[str]] = cfg.get("pools") or {}
    always: list[str] = list(cfg.get("always") or [])
    pick_n = int(cfg.get("pick", 2))

    tags = _match_tags(context)
    candidates: list[str] = list(always)
    for tag in tags:
        candidates.extend(pools.get(tag, []))
    # Dedup preserving order
    seen: set[str] = set()
    uniq: list[str] = []
    for item in candidates:
        if item not in seen:
            seen.add(item)
            uniq.append(item)

    seed = "|".join(
        [
            str(context.get("taken_at", "")),
            style_id,
            str(context.get("lat", "")),
            str(context.get("lon", "")),
            context.get("summary", ""),
        ]
    )
    rng = _rng(seed)
    if not uniq:
        uniq = [
            "subtle film grain",
            "soft vignette at the corners",
        ]
    n = max(1, min(pick_n, len(uniq)))
    chosen = rng.sample(uniq, n)

    context = dict(context)
    context["mood_tags"] = tags
    context["artifacts"] = chosen
    context["mood_watermark"] = (
        f"{context.get('summary', '')} · accent: {', '.join(chosen)}"
    )
    # Append accent softly so templates still see weather + one quirk.
    if context.get("summary"):
        context["summary"] = f"{context['summary']}; accent: {', '.join(chosen)}"
    return context
