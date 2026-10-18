from __future__ import annotations

from datetime import datetime, timezone
from typing import Any


def _parse_time(taken_at: str | None) -> datetime:
    if taken_at:
        try:
            return datetime.fromisoformat(taken_at.replace("Z", "+00:00"))
        except ValueError:
            pass
    return datetime.now(timezone.utc)


def time_of_day(when: datetime) -> str:
    """Wall-clock bucket of `when` (the offset it was sent with, not UTC)."""
    hour = when.hour
    if 5 <= hour < 8:
        return "dawn"
    if 8 <= hour < 12:
        return "morning"
    if 12 <= hour < 17:
        return "afternoon"
    if 17 <= hour < 20:
        return "dusk"
    return "night"


class StubContextProvider:
    """Deterministic fake science data so the pipeline runs offline."""

    def enrich(
        self,
        *,
        taken_at: str | None,
        client_ip: str | None,
        lat: float | None,
        lon: float | None,
        location_cfg: dict[str, Any],
        context_cfg: dict[str, Any],
    ) -> dict[str, Any]:
        when = _parse_time(taken_at)
        use_lat = lat if lat is not None else float(location_cfg.get("default_lat", 0))
        use_lon = lon if lon is not None else float(location_cfg.get("default_lon", 0))
        label = location_cfg.get("default_label", "unknown")
        # Use the timestamp's own clock. Z is UTC; an offset is that local hour.
        hour = when.hour
        is_night = hour < 6 or hour >= 20
        part = time_of_day(when)

        out: dict[str, Any] = {
            "taken_at": when.isoformat(),
            "client_ip": client_ip,
            "lat": use_lat,
            "lon": use_lon,
            "location_label": label,
            "is_night": is_night,
            "day_part": "night" if is_night else "day",
            "time_of_day": part,
            "time_basis": "capture" if taken_at else "server",
            "historical": False,
            "source": "stub",
        }
        if context_cfg.get("include_sun", True):
            out["sun"] = {
                "altitude_deg": -18.0 if is_night else 42.0,
                "description": "astronomical night" if is_night else "daylight",
            }
        if context_cfg.get("include_weather", True):
            out["weather"] = {
                "summary": "clear" if is_night else "partly cloudy",
                "temp_c": 11 if is_night else 18,
                "wind_ms": 2.5,
            }
        if context_cfg.get("include_astro", True):
            out["astro"] = {
                "moon_phase": "waxing gibbous",
                "bright_objects": ["Jupiter", "Orion"] if is_night else ["Sun"],
            }

        bits = [
            part,
            f"{out.get('weather', {}).get('summary', '')}",
            f"{out.get('weather', {}).get('temp_c', '')}C",
        ]
        if is_night and "astro" in out:
            bits.append("moon " + out["astro"]["moon_phase"])
            bits.append("stars " + ", ".join(out["astro"]["bright_objects"]))
        out["summary"] = ", ".join(b for b in bits if b)
        return out
