# Project Somnium

Hackathon AI camera: **ESP32-S3 edge client** + **laptop Python server**, linked
over **USB serial**.

Shoot on-device → JPEG goes to the ThinkPad over USB → laptop enriches with live
weather/sky mood → xAI edits → dream JPEG returns over USB for Review / Storage.

```
Camera ──USB COOK (JPEG + style)──► ThinkPad bridge → FastAPI /v1/process
                                               │
                         Open-Meteo + moon/sky mood
                                               │
                         mood artifacts → xAI prompt → xAI edit
                                               │
Camera ◄── USB DREAM (JPEG) ── Review + Storage (·raw / ·mod)
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

Weather and day/night map to two restrained lighting cues via `mood_artifacts`
in `creative/creative.json`. They form one secondary lighting sentence in the
edit prompt. Astronomy stays in metadata and does not enter the prompt.

Toggle with `include_weather` / `include_sun` / `include_astro` in
[`server/settings.json`](server/settings.json).

## Image pipeline

1. **Context** — Open-Meteo + local astro → mood summary + artifact tags
2. **Prompt** — xAI chat writes a short img2img *edit* instruction
3. **Edit** — xAI `/images/edits` grades the JPEG and downscales to 320×240
4. **Return** — dream over USB; orig + dream saved under `server/outputs/`

Camera-free dry run:

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

Review shows the dream; Storage dial alternates `·raw` / `·mod`.

Toolkit split:

- `lib/SomniumUi/` — page navigator, canvas, text, tweens
- `lib/SomniumApp/` — screens + generated Modes / theme / logo
- `lib/Somnium/` — camera, USB host protocol, SD, inputs, display

Preview the LCD UI on a laptop (no panel): `./scripts/ui_preview.sh`.

## Quick start

```sh
nix develop
cp .env.example .env   # put XAI_KEY=...

./scripts/flash.sh
./scripts/server.sh
./scripts/usb_bridge.sh
```

Keep the server and bridge in separate terminals while shooting. Stop the
bridge with Ctrl-C before flashing or pulling SD photos, then restart it.
Firmware and bridge must use the same USB protocol version. The bridge waits
for ESP acknowledgement before reporting a dream accepted.

Full checklist: `./scripts/dev.sh`

## Config

| File | What |
| --- | --- |
| [`creative/creative.json`](creative/creative.json) | Look & modes — theme, font, logo, filters, mood, providers |
| [`creative/logo.png`](creative/logo.png) | Boot logo source |
| [`server/settings.json`](server/settings.json) | Host/port, venue lat/lon, API URLs, context flags |
| [`.env`](.env.example) | Secrets (`XAI_KEY`) |

The camera's left/right correction is `Defaults::kCameraMirrorHorizontal` in
[`lib/Somnium/Config.h`](lib/Somnium/Config.h). It defaults to `true` and mirrors
the landscape image before JPEG encoding. If the sensor orientation changes
back, set it to `false`, rebuild, and reflash. The landscape framing stays the same.

After editing creative JSON / logo:

```sh
./scripts/sync_creative.sh   # → Modes.h, CreativeGen.h, LogoBitmap.h
```

## Day-to-day tools

```sh
./scripts/usb_bridge.sh                          # ESP ↔ localhost /v1/process
./scripts/ui_preview.sh                          # LCD UI on ThinkPad (no panel)
./scripts/cook.sh photo.jpg --style anime
python tests/monitor_inputs.py
python tests/capture_photo.py -o shot.jpg
```

## Layout

```
creative/          ← edit look, modes, prompts, mood artifacts, logo
server/            FastAPI brain (settings.json + providers)
lib/Somnium/       camera, USB COOK/DREAM, SD, inputs, display
lib/SomniumUi/     page / canvas toolkit
lib/SomniumApp/    screens (generated Modes/theme/logo headers)
scripts/           flash, server, usb_bridge, cook, ui_preview, sync_creative
tools/             cook.py, ui_preview.py, sync_creative.py
```

## Pins

| LCD | Encoder | Shutter |
| --- | --- | --- |
| SCK 1, MOSI 2, CS 21, DC 14, RST 47 | CLK 42, DT 41, SW 0 (BOOT) | GPIO 3 |

Keep the knob released during reset/flash (GPIO 0 = BOOT). Boot LCD should flash R→G→B→black.
