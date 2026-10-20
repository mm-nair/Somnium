# Pipeline

```
JPEG + style  (USB COOK from ESP)
        │
        ▼
 usb_bridge.py  →  POST /v1/process on localhost
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

`POST /v1/process` multipart: `image`, `style`, `taken_at`, optional `lat`/`lon`.

Response: JPEG + headers `X-Somnium-Prompt`, `X-Somnium-Context`, …

Local default is `http://127.0.0.1:8080/v1/process`. `SOMNIUM_SERVER_URL` points the USB bridge and `tools/cook.py` at another host. When `SOMNIUM_PROCESS_TOKEN` is set, the request must send it as `X-Somnium-Token` or `Authorization: Bearer`. `GET /health` stays open. Fly deploy: [server/README.md](server/README.md).

## Config map

| Concern | File |
| --- | --- |
| Modes / prompts / theme / providers | `creative/creative.json` |
| Venue, API base URLs, model ids | `server/settings.json`, overridable with `SOMNIUM_VENUE_*` |
| API key | `.env` → `XAI_KEY` (Fly: `fly secrets set`) |
| Process shared secret | `SOMNIUM_PROCESS_TOKEN` (required on Fly) |
| Server URL for the bridge | `SOMNIUM_SERVER_URL` or `--url` |

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
```
