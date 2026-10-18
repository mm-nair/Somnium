# Mood / space data providers

Used by `context` before the xAI prompt model runs.

| Signal | Provider | Cost / key | Notes |
| --- | --- | --- | --- |
| Weather (temp, clouds, wind, precip, WMO code) | [Open-Meteo](https://open-meteo.com) | Free, no key | `context_openmeteo.py` |
| Day / night (`is_day`) | Open-Meteo current | Free | Also sunrise / sunset / UV |
| Moon phase + illumination | Local Meeus-style math | Free / offline | Good enough for mood prompts |
| Bright sky objects | Heuristic from season + hour | Free / offline | Orion, Venus, Jupiter, … |
| Lat/lon default | `server/settings.json` venue | — | DubHacks coords |

## Swap / extend

1. Implement `providers/context_*.py` with an `enrich(...)` method.
2. Register in `providers/registry.py`.
3. Set `models.context_provider` in `creative/creative.json` or `context.provider` in `settings.json`.

Future free options worth plugging in: NASA/JPL Horizons (precise ephemerides), USNO, Sunrise-Sunset.org, OpenAQ (air quality).
