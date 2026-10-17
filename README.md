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
cp server/config.example.yaml server/config.yaml
# edit the three files (SSID, laptop IP, prompts/models)

./scripts/server.sh       # terminal A — note the IP
./scripts/flash.sh        # terminal B
```

Encoder cycles style; shutter captures → uploads → LCD + SD.

**Local-only (no server):** `#define SOMNIUM_USE_SERVER 0` in `include/server_config.h`, re-flash. See [PIPELINE.md](PIPELINE.md).

## Config you will edit

| File | Purpose |
| --- | --- |
| `include/wifi_credentials.h` | Hotspot SSID/password |
| `include/server_config.h` | Laptop IP + `SOMNIUM_USE_SERVER` |
| `server/config.yaml` | Styles, prompt text, model providers |

Swap models in YAML only (`prompt.provider`, `image.provider`). Default image provider is `echo` (returns the same photo) so the pipe works before you wire Grok/Nano Banana/etc.

## Layout

```
lib/Somnium/       hardware + ApiClient + Storage + TimeSync
lib/SomniumUi/     page UI toolkit (screens later)
server/            FastAPI brain (config-driven providers)
scripts/           flash.sh, server.sh, dev.sh
tests/             serial hardware checks
PIPELINE.md        HTTP contract + how to revert/swap
```

## Hardware tests

```sh
python tests/monitor_inputs.py
python tests/capture_photo.py -o shot.jpg
```

## Pins

| LCD | Encoder | Shutter |
| --- | --- | --- |
| SCK 21, MOSI 47, DC 14, CS 41, RST 48 | CLK 1, DT 2, SW 42 | GPIO 3 |
