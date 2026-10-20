# Pipeline

```
JPEG + style  (COOK from ESP, over USB serial or TCP :8090)
        │
        ▼
 usb_bridge.py or wifi_bridge.py  →  POST /v1/process on localhost
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
  *_0_orig.jpg  +  *_1_dream.jpg
  USB DREAM → ESP Review / Storage
```

## USB framing

ESP → laptop:

```
COOK style=<style> len=<n> id=<request> crc=<crc32> lat=… lon=…
<n jpeg bytes>
COOK_END
```

Laptop → ESP:

```
DREAM <request> <n> <crc32>
```

ESP replies after allocating its receive buffer:

```
READY DREAM <request>
```

Only then does the laptop send:

```
<n jpeg bytes>
DREAM_END
```

Both binary payloads are followed by a newline, their end marker, and a
newline. Request IDs and CRC32 values are eight hexadecimal digits. The CRC is
the standard CRC32 used by Python `zlib.crc32` and ESP `esp_rom_crc32_le(0, …)`.
The ESP verifies the length, trailer, CRC and JPEG markers before replying
`ACK DREAM <request>`. Errors use `ERR <request> <reason>`.

The bridge reports acceptance only after that ACK. A reset or disconnect
aborts the current transfer; reconnecting never replays its binary payload.
Flash firmware and restart the bridge together when updating this protocol.

The same bytes run over TCP when a laptop is connected to the board. The
board listens on port 8090 and advertises `somnium.local`.
`scripts/wifi_bridge.py` is the TCP peer; `scripts/usb_bridge.py` remains the
USB peer. A cook uses Wi-Fi when that client is connected, and USB otherwise.
Wi-Fi name and password live in `include/wifi_secrets.h` (see
`include/wifi_secrets.example.h`), not in git.

An empty `SOMNIUM_SERVER_HOST` in `include/server_config.h` keeps that path
and sends no process token. A non-empty host makes the board POST
`/v1/process` itself. `SOMNIUM_SERVER_TLS 1` is HTTPS on port 443. The board
checks the Let's Encrypt certificate and waits up to 300 seconds for the
JPEG. `SOMNIUM_PROCESS_TOKEN`, when set, is the `X-Somnium-Token` header.
The laptop bridges read the same token and `SOMNIUM_SERVER_URL` from the
environment. Their HTTP timeout stays under 300 seconds. Caddy on the VM
waits 360 seconds.

When the camera has both a gallery URL and a gallery token, the cook form
includes `skip_gallery=1` and the camera uploads the shot. The server honors
that field (`1`, `true`, `yes`, or `on`) and does not start its own gallery
upload. With the camera gallery settings empty, the server uploads once when
its upload URL and token are set.

## Firmware JPEG decoding

Use TJpg_Decoder throughout the display path with native RGB565 pixels.
Do not combine its wrapper with the camera library's `jpg2rgb565`: both
libraries export `jd_prepare` and `jd_decomp`, but their decoder structures
and pixel formats differ. The firmware link-map regression checks that both
symbols come from TJpg_Decoder.

Review starts its display timer after the decoded image is drawn. Failed
decoding shows `image unavailable` and logs the failure. Storage lists SD
photos together with RAM fallback copies for failed or unavailable SD saves.
New shot numbers skip existing pairs across reboots. The on-device SD index
keeps the latest 64 files.

Build and run the offline regressions:

```sh
nix develop -c pio run
nix develop -c python -m unittest discover -s tests -p 'test_*.py' -v
```

## HTTP

`POST /v1/process` multipart: `image`, `style`, `taken_at`, optional `lat`/`lon`, optional `skip_gallery`.

Response: JPEG + headers `X-Somnium-Prompt`, `X-Somnium-Context`, …

Local default is `http://127.0.0.1:8080/v1/process`. `SOMNIUM_SERVER_URL` points the USB bridge, the Wi-Fi bridge, and `tools/cook.py` at another host. When `SOMNIUM_PROCESS_TOKEN` is set, the request must send it as `X-Somnium-Token` or `Authorization: Bearer`. Unset, the laptop server stays open. `skip_gallery=1` tells the server the camera is uploading the shot. `GET /health` stays open. The board's cook budget is 300 seconds. A bridge timeout at or above that is capped to 290 seconds so the bridge can still send `ERR`. Caddy on the Oracle VM waits 360 seconds. Deploy notes: [server/README.md](server/README.md).

## Config map

| Concern | File |
| --- | --- |
| Modes / prompts / theme / providers | `creative/creative.json` |
| Venue, API base URLs, model ids | `server/settings.json`, overridable with `SOMNIUM_VENUE_*` |
| API key | `.env` → `XAI_KEY` (Oracle: `deploy/.env`) |
| Process shared secret | `SOMNIUM_PROCESS_TOKEN` (required on the public VM, unset on the laptop) |
| Server URL for the bridges | `SOMNIUM_SERVER_URL` or `--url` |
| Wi-Fi SSID / password | `include/wifi_secrets.h` (gitignored) |
| Direct cook host, TLS, tokens | `include/server_config.h` (gitignored) |

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
```
