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
`/v1/process` itself. `SOMNIUM_SERVER_TLS 1` is HTTPS on the configured port
(443 for a public host such as Fly). `SOMNIUM_PROCESS_TOKEN`, when set, is
the `X-Somnium-Token` header. The laptop bridges read the same token and
`SOMNIUM_SERVER_URL` from the environment.

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

## HTTP (laptop local)

`POST /v1/process` multipart: `image`, `style`, `taken_at`, optional `lat`/`lon`.

Response: JPEG + headers `X-Somnium-Prompt`, `X-Somnium-Context`, …

## Config map

| Concern | File |
| --- | --- |
| Modes / prompts / theme / providers | `creative/creative.json` |
| Venue, API base URLs, model ids | `server/settings.json` |
| API key | `.env` → `XAI_KEY` |
| Wi-Fi SSID / password | `include/wifi_secrets.h` (gitignored) |

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
```
