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
        hour = when.astimezone(timezone.utc).hour
        is_night = hour < 6 or hour >= 20

        out: dict[str, Any] = {
            "taken_at": when.isoformat(),
            "client_ip": client_ip,
            "lat": use_lat,
            "lon": use_lon,
            "location_label": label,
            "is_night": is_night,
            "day_part": "night" if is_night else "day",
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
            f"{out['day_part']}",
            f"{out.get('weather', {}).get('summary', '')}",
            f"{out.get('weather', {}).get('temp_c', '')}C",
        ]
        if is_night and "astro" in out:
            bits.append("moon " + out["astro"]["moon_phase"])
            bits.append("stars " + ", ".join(out["astro"]["bright_objects"]))
        out["summary"] = ", ".join(b for b in bits if b)
        return out
