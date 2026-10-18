"""Live mood context from free/open APIs (no paid keys).

Providers used
--------------
* Open-Meteo (https://open-meteo.com) — weather, is_day, clouds, wind,
  sunrise/sunset. Free, no API key, non-commercial OK.
* Local astronomy math — moon phase / illumination (Meeus-style approx).
* Heuristic bright-planet list from local solar time (no key).

Fallbacks to StubContextProvider if the network call fails.
"""

from __future__ import annotations

import json
import math
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from typing import Any

from somnium_server.providers.context_stub import (
    StubContextProvider,
    _parse_time,
    time_of_day,
)

# WMO weather interpretation codes (subset) → short English.
_WMO = {
    0: "clear",
    1: "mainly clear",
    2: "partly cloudy",
    3: "overcast",
    45: "fog",
    48: "rime fog",
    51: "light drizzle",
    61: "light rain",
    63: "rain",
    65: "heavy rain",
    71: "light snow",
    73: "snow",
    75: "heavy snow",
    80: "rain showers",
    95: "thunderstorm",
}


def _moon(when: datetime) -> dict[str, Any]:
    """Approximate synodic moon phase (no external call)."""
    # Julian day
    y, m, d = when.year, when.month, when.day + when.hour / 24.0
    if m <= 2:
        y -= 1
        m += 12
    a = int(y / 100)
    b = 2 - a + int(a / 4)
    jd = int(365.25 * (y + 4716)) + int(30.6001 * (m + 1)) + d + b - 1524.5
    # Days since known new moon (2000-01-06)
    days = jd - 2451550.1
    age = days % 29.530588853
    phase = age / 29.530588853
    illum = 0.5 * (1.0 - math.cos(2.0 * math.pi * phase))
    if phase < 0.03 or phase > 0.97:
        name = "new"
    elif phase < 0.22:
        name = "waxing crescent"
    elif phase < 0.28:
        name = "first quarter"
    elif phase < 0.47:
        name = "waxing gibbous"
    elif phase < 0.53:
        name = "full"
    elif phase < 0.72:
        name = "waning gibbous"
    elif phase < 0.78:
        name = "last quarter"
    else:
        name = "waning crescent"
    return {
        "phase": name,
        "illumination": round(illum, 2),
        "age_days": round(age, 1),
    }


def _bright_objects(when: datetime, is_night: bool, lat: float) -> list[str]:
    """Cheap visibility hints for prompt mood (not precise ephemerides)."""
    hour = when.hour + when.minute / 60.0
    objs: list[str] = []
    if is_night:
        objs.append("Moon")
        # Rough seasons for northern-hemisphere winter/summer bright stars.
        month = when.month
        if month in (11, 12, 1, 2):
            objs.extend(["Orion", "Sirius"])
        elif month in (3, 4, 5):
            objs.extend(["Leo", "Arcturus"])
        elif month in (6, 7, 8):
            objs.extend(["Summer Triangle", "Vega"])
        else:
            objs.extend(["Pegasus", "Fomalhaut"])
        # Venus as morning/evening star heuristic
        if 4.5 <= hour <= 7.5 or 17.5 <= hour <= 21.5:
            objs.insert(1, "Venus")
        if abs(lat) > 20 and month in (9, 10, 11, 12, 1, 2, 3):
            objs.append("Jupiter")
    else:
        objs.append("Sun")
    return objs[:5]


def _now() -> datetime:
    return datetime.now(timezone.utc)


def _as_utc(when: datetime) -> datetime:
    if when.tzinfo is None:
        return when.replace(tzinfo=timezone.utc)
    return when.astimezone(timezone.utc)


def _history_after_seconds(context_cfg: dict[str, Any]) -> float:
    try:
        minutes = float(context_cfg.get("history_after_minutes", 90))
    except (TypeError, ValueError):
        minutes = 90.0
    if minutes < 0:
        minutes = 0.0
    return minutes * 60.0


def capture_is_historical(
    taken_at: str | None,
    when: datetime,
    context_cfg: dict[str, Any],
) -> bool:
    """True when the shot is old enough that 'current weather' would be the wrong hour."""
    if not taken_at:
        return False
    age = (_now() - _as_utc(when)).total_seconds()
    return age >= _history_after_seconds(context_cfg)


def _fetch_open_meteo(lat: float, lon: float) -> dict[str, Any]:
    params = urllib.parse.urlencode(
        {
            "latitude": f"{lat:.4f}",
            "longitude": f"{lon:.4f}",
            "current": (
                "temperature_2m,relative_humidity_2m,apparent_temperature,"
                "is_day,precipitation,weather_code,cloud_cover,wind_speed_10m,"
                "surface_pressure"
            ),
            "daily": "sunrise,sunset,uv_index_max,daylight_duration",
            "timezone": "auto",
            "wind_speed_unit": "ms",
            "forecast_days": 1,
        }
    )
    url = f"https://api.open-meteo.com/v1/forecast?{params}"
    req = urllib.request.Request(url, headers={"User-Agent": "Somnium/0.1"})
    with urllib.request.urlopen(req, timeout=8) as resp:
        return json.loads(resp.read().decode())


# Recent hours: historical forecast (previous model runs). Older hours: ERA5 archive.
_HISTORY_URLS = (
    ("open-meteo-history", "https://historical-forecast-api.open-meteo.com/v1/forecast"),
    ("open-meteo-archive", "https://archive-api.open-meteo.com/v1/archive"),
)


def _hourly_at(hourly: dict[str, Any], key: str, index: int) -> Any:
    series = hourly.get(key) or []
    if not isinstance(series, list) or index >= len(series):
        return None
    return series[index]


def _normalize_history(raw: dict[str, Any], when: datetime) -> dict[str, Any]:
    """Pick the hourly sample nearest the capture instant and shape it like `current`."""
    hourly = raw.get("hourly") or {}
    times = hourly.get("time") or []
    if not isinstance(times, list) or not times:
        raise ValueError("historical response has no hourly times")
    offset = timedelta(seconds=int(raw.get("utc_offset_seconds") or 0))
    target = _as_utc(when)
    best_index = None
    best_delta = None
    best_local = None
    best_utc = None
    for index, stamp in enumerate(times):
        if not isinstance(stamp, str):
            continue
        local = datetime.fromisoformat(stamp)
        utc = (local - offset).replace(tzinfo=timezone.utc)
        delta = abs((utc - target).total_seconds())
        if best_delta is None or delta < best_delta:
            best_index = index
            best_delta = delta
            best_local = local
            best_utc = utc
    if best_index is None or best_delta is None or best_delta > 90 * 60:
        raise ValueError("no hourly sample within 90 minutes of capture")
    code = _hourly_at(hourly, "weather_code", best_index)
    current = {
        "temperature_2m": _hourly_at(hourly, "temperature_2m", best_index),
        "relative_humidity_2m": _hourly_at(hourly, "relative_humidity_2m", best_index),
        "apparent_temperature": _hourly_at(hourly, "apparent_temperature", best_index),
        "is_day": _hourly_at(hourly, "is_day", best_index),
        "precipitation": _hourly_at(hourly, "precipitation", best_index),
        "weather_code": code,
        "cloud_cover": _hourly_at(hourly, "cloud_cover", best_index),
        "wind_speed_10m": _hourly_at(hourly, "wind_speed_10m", best_index),
        "surface_pressure": _hourly_at(hourly, "surface_pressure", best_index),
    }
    daily = raw.get("daily") or {}
    daily_times = daily.get("time") or []
    local_date = best_local.date().isoformat() if best_local is not None else ""
    daily_index = daily_times.index(local_date) if local_date in daily_times else 0

    def one(key: str) -> list[Any]:
        series = daily.get(key) or []
        if isinstance(series, list) and daily_index < len(series):
            return [series[daily_index]]
        return [None]

    return {
        "current": current,
        "daily": {
            "sunrise": one("sunrise"),
            "sunset": one("sunset"),
            "uv_index_max": one("uv_index_max"),
            "daylight_duration": one("daylight_duration"),
        },
        "weather_at": best_utc.isoformat() if best_utc is not None else None,
        "time_of_day": time_of_day(best_local) if best_local is not None else time_of_day(when),
    }


def _history_request(url: str, lat: float, lon: float, start: str, end: str) -> dict[str, Any]:
    params = urllib.parse.urlencode(
        {
            "latitude": f"{lat:.4f}",
            "longitude": f"{lon:.4f}",
            "start_date": start,
            "end_date": end,
            "hourly": (
                "temperature_2m,relative_humidity_2m,apparent_temperature,"
                "is_day,precipitation,weather_code,cloud_cover,wind_speed_10m,"
                "surface_pressure"
            ),
            "daily": "sunrise,sunset,uv_index_max,daylight_duration",
            "timezone": "auto",
            "wind_speed_unit": "ms",
        }
    )
    req = urllib.request.Request(
        f"{url}?{params}", headers={"User-Agent": "Somnium/0.2"}
    )
    with urllib.request.urlopen(req, timeout=8) as resp:
        return json.loads(resp.read().decode())


def _fetch_historical(lat: float, lon: float, when: datetime) -> dict[str, Any]:
    utc_day = _as_utc(when).date()
    start = (utc_day - timedelta(days=1)).isoformat()
    end = (utc_day + timedelta(days=1)).isoformat()
    errors: list[str] = []
    for name, url in _HISTORY_URLS:
        try:
            raw = _history_request(url, lat, lon, start, end)
            normalized = _normalize_history(raw, when)
        except (
            urllib.error.URLError,
            TimeoutError,
            json.JSONDecodeError,
            KeyError,
            ValueError,
            IndexError,
        ) as err:
            errors.append(f"{name}: {err}")
            continue
        normalized["history_source"] = name
        return normalized
    raise urllib.error.URLError("historical weather failed: " + "; ".join(errors))


class OpenMeteoContextProvider:
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

        historical = capture_is_historical(taken_at, when, context_cfg)
        try:
            if historical:
                raw = _fetch_historical(use_lat, use_lon, when)
            else:
                raw = _fetch_open_meteo(use_lat, use_lon)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError, ValueError):
            fallback = StubContextProvider().enrich(
                taken_at=taken_at,
                client_ip=client_ip,
                lat=lat,
                lon=lon,
                location_cfg=location_cfg,
                context_cfg=context_cfg,
            )
            fallback["historical"] = historical
            fallback["time_of_day"] = time_of_day(when)
            fallback["time_basis"] = "capture" if taken_at else "server"
            fallback["source"] = "stub"
            return fallback

        cur = raw.get("current") or {}
        daily = raw.get("daily") or {}
        code = int(cur.get("weather_code") or 0)
        is_day = bool(cur.get("is_day", 1))
        is_night = not is_day
        weather_summary = _WMO.get(code, f"code {code}")
        part = raw.get("time_of_day") or time_of_day(when)

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
            "historical": historical,
            "source": raw.get("history_source") or "open-meteo",
        }
        if raw.get("weather_at"):
            out["weather_at"] = raw["weather_at"]

        if context_cfg.get("include_weather", True):
            out["weather"] = {
                "summary": weather_summary,
                "temp_c": cur.get("temperature_2m"),
                "feels_like_c": cur.get("apparent_temperature"),
                "humidity_pct": cur.get("relative_humidity_2m"),
                "cloud_cover_pct": cur.get("cloud_cover"),
                "wind_ms": cur.get("wind_speed_10m"),
                "precip_mm": cur.get("precipitation"),
                "pressure_hpa": cur.get("surface_pressure"),
                "wmo_code": code,
            }

        if context_cfg.get("include_sun", True):
            sunrise = (daily.get("sunrise") or [None])[0]
            sunset = (daily.get("sunset") or [None])[0]
            out["sun"] = {
                "is_day": is_day,
                "sunrise": sunrise,
                "sunset": sunset,
                "uv_index_max": (daily.get("uv_index_max") or [None])[0],
                "daylight_hours": (daily.get("daylight_duration") or [None])[0],
                "description": "daylight" if is_day else "night",
            }

        if context_cfg.get("include_astro", True):
            moon = _moon(when.astimezone(timezone.utc))
            out["astro"] = {
                "moon_phase": moon["phase"],
                "moon_illumination": moon["illumination"],
                "moon_age_days": moon["age_days"],
                "bright_objects": _bright_objects(
                    when.astimezone(timezone.utc), is_night, use_lat
                ),
            }

        bits: list[str] = [part, weather_summary]
        w = out.get("weather") or {}
        if w.get("temp_c") is not None:
            bits.append(f"{w['temp_c']}C")
        if w.get("cloud_cover_pct") is not None:
            bits.append(f"{w['cloud_cover_pct']}% cloud")
        if is_night and "astro" in out:
            bits.append("moon " + out["astro"]["moon_phase"])
            bits.append(
                "sky " + ", ".join(out["astro"]["bright_objects"])
            )
        elif "sun" in out and out["sun"].get("sunrise"):
            bits.append(f"sunrise {out['sun']['sunrise']}")
        # Never put venue/event names in summary — models paint them as text.
        out["summary"] = ", ".join(str(b) for b in bits if b)
        return out
