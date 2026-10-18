# Pipeline contract (revertable)

Edge (ESP32 C++) and brain (laptop Python) talk over one HTTP call.
Either side can be replaced without rewriting the other.

## Flow

```
JPEG + style + taken_at
        │
        ▼
┌───────────────────┐
│ Context (Open-Meteo│  weather, day/night, moon, sky mood
│  + local astro)    │
└─────────┬─────────┘
          ▼
┌───────────────────┐
│ Prompt (xAI Grok) │  style keywords + mood → img2img prompt
└─────────┬─────────┘
          ▼
┌───────────────────┐
│ Image (xAI Imagine│  edit capture with that prompt
│  /images/edits)   │
└─────────┬─────────┘
          ▼
  save *_0_orig.jpg  +  *_1_dream.jpg   (two slideshow entries)
  return dream JPEG to camera for review glance
```

## Request

`POST /v1/process` `multipart/form-data`

| Field | Required | Notes |
| --- | --- | --- |
| `image` | yes | JPEG |
| `style` | no | `cartoon` / `cyberpunk` / … (`creative/creative.json`) |
| `taken_at` | no | ISO-8601 UTC (NTP on device, or `--at` in cook) |
| `lat` / `lon` | no | optional overrides |
| `force_real` | no | `1` to force model call even if `real: false` |

## Response

- `200` `image/jpeg` body (dream / modified image)
- Headers: `X-Somnium-Prompt`, `X-Somnium-Context`, `X-Somnium-Style`,
  `X-Somnium-Orig`, `X-Somnium-Dream`
- Errors: JSON `{"error":"..."}` with 4xx/5xx

## Dual slideshow entries

Each cook/shot writes **two** files (sorted so the dial alternates):

- `{stamp}_{style}_0_orig.jpg` — unmodified capture
- `{stamp}_{style}_1_dream.jpg` — model output

Storage labels show `NOIR·raw` / `NOIR·mod` and `i/len(photos)`.

## Camera-free test

```bash
nix develop
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
# outputs under server/outputs/
```

## Secrets

Put `XAI_KEY=...` in repo `.env` (gitignored). `scripts/server.sh` and
`scripts/cook.sh` load it.

## Swap models

1. Implement `providers/image_*.py` or `prompt_*.py` / `context_*.py`
2. Register in `providers/registry.py`
3. Edit `creative/creative.json` → `models` and/or `server/settings.json`

See also `server/DATA.md` for free space/weather APIs.
