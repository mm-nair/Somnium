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
COOK style=<style> len=<n> id=<request> crc=<crc32> lat=… lon=… device=<id> board=<name>
<n jpeg bytes>
COOK_END
```

`device` and `board` are optional. Older firmware omits them. Tokens are
letters, digits, `.`, `_`, and `-`. The bridge forwards them as `device_id`
and `board` on `POST /v1/process`. Details: [server/MULTI_CAMERA.md](server/MULTI_CAMERA.md).

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

## HTTP (laptop local)

`POST /v1/process` multipart: `image`, `style`, `taken_at`, optional `lat`/`lon`,
optional `device_id` and `board`.

Response: JPEG + headers `X-Somnium-Prompt`, `X-Somnium-Context`,
`X-Somnium-Style`, and `X-Somnium-Device` / `X-Somnium-Board` when set.

Style registry and scene-preservation eval: [server/STYLE_ENGINE.md](server/STYLE_ENGINE.md).

```sh
PYTHONPATH=server python -m somnium_server.eval pair orig.jpg dream.jpg
PYTHONPATH=server python -m somnium_server.eval mock photo.jpg --style pencil
SOMNIUM_EVAL_LIVE=1 PYTHONPATH=server python -m somnium_server.eval live photo.jpg
```

| Method | Path |
| --- | --- |
| GET | `/v1/styles`, `/v1/styles/{id}` |
| POST | `/v1/styles/reload` |
| GET, POST, PATCH | `/v1/devices`, `/v1/devices/{id}` |
| GET, POST | `/v1/boards`, `/v1/boards/{id}`, `/v1/boards/{id}/shots` |

## Config map

| Concern | File |
| --- | --- |
| Modes / prompts / theme / providers | `creative/creative.json` |
| Venue, API base URLs, model ids | `server/settings.json` |
| API key | `.env` → `XAI_KEY` |

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style watercolor --at 2026-10-17T19:30:00Z
```
