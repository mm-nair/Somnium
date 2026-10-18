# Pipeline

```
JPEG + style + taken_at
        │
        ▼
 Context (Open-Meteo weather / day-night + local moon mood)
        │
        ▼
 Prompt (xAI chat) — short FILTER instruction
        │
        ▼
 Image (xAI /images/edits) — grade only, resized to input (320×240)
        │
        ▼
  *_0_orig.jpg  +  *_1_dream.jpg   → slideshow alternates on dial
  response body = dream JPEG
```

## HTTP

`POST /v1/process` multipart: `image`, `style`, `taken_at`, optional `lat`/`lon`/`force_real`.

Response: JPEG + headers `X-Somnium-Prompt`, `X-Somnium-Context`, …

## Config map

| Concern | File |
| --- | --- |
| Modes / prompts / theme / which providers | `creative/creative.json` |
| Venue, API base URLs, model ids | `server/settings.json` |
| API key | `.env` → `XAI_KEY` |

## Free mood data

| Signal | Source |
| --- | --- |
| Weather, `is_day`, sunrise/sunset | [Open-Meteo](https://open-meteo.com) (no key) |
| Moon phase | Local math in `context_openmeteo.py` |
| Bright-sky hints | Heuristic from season/hour |

Swap providers in `server/somnium_server/providers/` + register in `registry.py`, then set `models.*` in `creative.json`.

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
```

## Offline camera

`#define SOMNIUM_USE_SERVER 0` in `include/server_config.h`, re-flash.
