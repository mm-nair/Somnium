# Project Somnium

Hackathon AI camera: **ESP32-S3 edge client** + **laptop Python server**.

```
Camera ──JPEG + style + time──► Laptop
                                  │
                    mood → xAI prompt → xAI filter
                                  │
Camera ◄── review + SD (orig + dream) ◄─┘
```

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
| [`creative/creative.json`](creative/creative.json) | **Look & modes** — theme colors, font, logo, filters, model names |
| [`creative/logo.png`](creative/logo.png) | Boot logo source |
| [`server/settings.json`](server/settings.json) | Host/port, venue lat/lon, API URLs |
| [`.env`](.env.example) | Secrets (`XAI_KEY`) |
| `include/wifi_credentials.h` | Hotspot SSID/pass |
| `include/server_config.h` | Laptop IP + `SOMNIUM_USE_SERVER` |

After editing creative JSON / logo:

```sh
./scripts/sync_creative.sh   # → Modes.h, CreativeGen.h, LogoBitmap.h
```

Details: [`creative/README.md`](creative/README.md). Pipeline: [`PIPELINE.md`](PIPELINE.md).

## Day-to-day tools

```sh
./scripts/ui_preview.sh                          # LCD UI on ThinkPad (no panel)
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
python tests/monitor_inputs.py
python tests/capture_photo.py -o shot.jpg
```

## Layout

```
creative/          ← edit look, modes, prompts, logo
server/            FastAPI brain (settings.json + providers)
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
