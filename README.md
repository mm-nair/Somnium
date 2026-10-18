# Project Somnium

Hackathon AI camera: **ESP32-S3 edge client** + **laptop Python server**.

Shoot on-device → laptop enriches the shot with live weather and sky mood →
xAI writes a short edit prompt → xAI restyles the JPEG → dream comes back for
review and SD.

```
Camera ──JPEG + style + UTC time + lat/lon──► Laptop
                                               │
                         Open-Meteo + moon/sky mood
                                               │
                         mood artifacts → xAI prompt → xAI edit
                                               │
Camera ◄── Review (dream) + Storage (·raw / ·mod) ◄─┘
```

Deep dive: [`PIPELINE.md`](PIPELINE.md). Creative look & modes: [`creative/README.md`](creative/README.md).

## Mood & sky context

Before any model runs, the server builds a short mood summary from free data
(no paid keys for this step):

| Signal | Source |
| --- | --- |
| Weather, clouds, wind, `is_day` | [Open-Meteo](https://open-meteo.com) forecast API |
| Sunrise / sunset, UV, daylight | Open-Meteo daily fields |
| Moon phase / illumination | Local Meeus-style math (`context_openmeteo.py`) |
| Bright-sky hints (Venus, Orion, …) | Heuristic from season, hour, latitude |

Those signals map to one subtle optical accent via `mood_artifacts` in
`creative/creative.json` (soft glare, grain, cool tint, moonlit lift, …). Style
grade leads; mood is a quiet accent — not a scene rewrite.

Toggle with `include_weather` / `include_sun` / `include_astro` in
[`server/settings.json`](server/settings.json). Network failure falls back to a
stub context so the camera still works.

## Image pipeline

1. **Context** — Open-Meteo + local astro → mood summary + artifact tags
2. **Prompt** — xAI chat (`openai_compat`) writes a short img2img *edit*
   instruction (same crop, grade + optical mood, no new scenery/text)
3. **Edit** — xAI `/images/edits` (`grok-imagine-image-…`) grades the JPEG and
   downscales back to the camera resolution (320×240)
4. **Return** — dream JPEG in the response body; orig + dream saved for Storage

Provider swap lives in `creative/creative.json` → `models.*` and
`server/somnium_server/providers/`. Camera-free dry run:

```sh
./scripts/cook.sh photo.jpg --style anime --at 2026-10-17T19:30:00Z
```

## Edge UI

Firmware screens: **Boot → Modes → Process → Review → Storage**.

| Input | Action |
| --- | --- |
| Shutter | Always shoot (current mode) |
| Dial turn | Modes carousel / browse Storage |
| Dial click | Modes ↔ Storage |

Review shows the dream; Storage dial alternates `·raw` / `·mod` (RAM gallery,
SD card when present). Look is driven by `creative/creative.json` (theme colors,
Adafruit GFX font, logo) and synced into firmware headers.

Toolkit split:

- `lib/SomniumUi/` — page navigator, canvas, text, tweens
- `lib/SomniumApp/` — screens + generated Modes / theme / logo
- `lib/Somnium/` — camera, Wi‑Fi, SD, HTTP client, inputs, display

Preview the LCD UI on a laptop (no panel): `./scripts/ui_preview.sh`.

## Quick start

```sh
nix develop
cp include/wifi_credentials.example.h include/wifi_credentials.h
cp include/server_config.example.h include/server_config.h
cp .env.example .env   # put XAI_KEY=...

./scripts/server.sh    # terminal A — note hotspot IP → server_config.h
./scripts/flash.sh     # terminal B
```

Full checklist: `./scripts/dev.sh`

## Config (only these)

| File | What |
| --- | --- |
| [`creative/creative.json`](creative/creative.json) | **Look & modes** — theme, font, logo, filters, mood artifacts, providers |
| [`creative/logo.png`](creative/logo.png) | Boot logo source |
| [`server/settings.json`](server/settings.json) | Host/port, venue lat/lon, API URLs, context flags |
| [`.env`](.env.example) | Secrets (`XAI_KEY`) |
| `include/wifi_credentials.h` | Hotspot SSID/pass |
| `include/server_config.h` | Laptop IP + `SOMNIUM_USE_SERVER` |

After editing creative JSON / logo:

```sh
./scripts/sync_creative.sh   # → Modes.h, CreativeGen.h, LogoBitmap.h
```

**Full Wi‑Fi path:** laptop on hotspot → `./scripts/server.sh` → set
`SOMNIUM_SERVER_HOST` → flash → shutter.

## Day-to-day tools

```sh
./scripts/wifi_test.sh                           # check server + print IP for ESP
./scripts/ui_preview.sh                          # LCD UI on ThinkPad (no panel)
./scripts/cook.sh photo.jpg --style anime --at 2026-10-17T19:30:00Z
python tests/monitor_inputs.py
python tests/capture_photo.py -o shot.jpg
```

Offline camera (no laptop): `#define SOMNIUM_USE_SERVER 0` in
`include/server_config.h`, then re-flash.

## Layout

```
creative/          ← edit look, modes, prompts, mood artifacts, logo
server/            FastAPI brain (settings.json + providers)
  providers/       openmeteo context, xAI prompt/edit, stubs
lib/Somnium/       hardware, Wi‑Fi, SD, HTTP client
lib/SomniumUi/     page / canvas toolkit
lib/SomniumApp/    screens (generated Modes/theme/logo headers)
scripts/           flash, server, cook, ui_preview, sync_creative
tools/             cook.py, ui_preview.py, sync_creative.py
```

## Pins

| LCD | Encoder | Shutter |
| --- | --- | --- |
| SCK 1, MOSI 2, CS 21, DC 14, RST 47 | CLK 42, DT 41, SW 0 (BOOT) | GPIO 3 |

Keep the knob released during reset/flash (GPIO 0 = BOOT). Boot LCD should flash R→G→B→black.
