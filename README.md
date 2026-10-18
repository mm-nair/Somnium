# Project Somnium

Hackathon AI camera: **ESP32-S3 edge client** + **laptop Python server** on a phone hotspot.

```
Camera ──JPEG+style+time──► Laptop server ──context→prompt→img2img──► JPEG
   ▲                              │
   └──────── display + SD ←───────┘
```

## Quick start

```sh
nix develop
./scripts/dev.sh          # prints the full checklist

# typical loop:
cp include/wifi_credentials.example.h include/wifi_credentials.h
cp include/server_config.example.h include/server_config.h
# edit wifi + laptop IP; edit creative/creative.json for modes/prompts

./scripts/server.sh       # terminal A — note the IP
./scripts/flash.sh        # terminal B
```

UI: boot splash → mode carousel (dial) → shutter → processing → result. Date/time in the header/footer.

**Local-only (no server):** `#define SOMNIUM_USE_SERVER 0` in `include/server_config.h`, re-flash. See [PIPELINE.md](PIPELINE.md).

## Config you will edit

| File | Purpose |
| --- | --- |
| `creative/creative.json` | Modes, prompts, theme notes, model provider names |
| `server/settings.json` | Host/port, venue lat/lon, API endpoints |
| `include/wifi_credentials.h` | Hotspot SSID/password |
| `include/server_config.h` | Laptop IP + `SOMNIUM_USE_SERVER` |
| `lib/SomniumApp/Modes.h` | Camera-side mode ids/labels (keep in sync with JSON) |

Font swap: `lib/SomniumApp/Brand.h` (`FreeSerifBoldItalic12pt7b`). Logo: `assets/somnium_logo.png`.

## Layout

```
lib/Somnium/       hardware + ApiClient + Storage + TimeSync
lib/SomniumUi/     page / canvas / tween toolkit
lib/SomniumApp/    Somnium screens (boot, carousel, process, result)
creative/          creative.json — edit prompts & modes here
server/            FastAPI brain
scripts/           flash.sh, server.sh, dev.sh
tests/             serial hardware checks
```

## Hardware tests

```sh
python tests/monitor_inputs.py
python tests/capture_photo.py -o shot.jpg
```

## Pins

| LCD | Encoder | Shutter |
| --- | --- | --- |
| SCK 1, MOSI 2, CS 21, DC 14, RST 47 | CLK 42, DT 41, SW 0 (BOOT) | GPIO 3 |

Same pin map as the working Hackathon_Prep `camera_dual` / `controls` sketches. Keep the knob released during reset/flash (GPIO 0 = BOOT). On boot the LCD should flash red→green→blue→black; solid white means the panel isn’t getting SPI.
