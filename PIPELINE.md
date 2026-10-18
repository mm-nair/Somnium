# Pipeline

```
 shutter JPEG
      │
      ▼
 Transport (Wi-Fi primary, USB fallback)
      │
      ├── Wi-Fi up ── POST /v1/process multipart (source=wifi)
      │                 optional gallery POST (Bearer token, from NVS)
      │
      └── Wi-Fi down or HTTP failed
                    └── USB COOK frame → usb_bridge.py → same /v1/process
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
 dream JPEG → ESP Review / Storage
```

```
 no creds ───────────────► Portal (SoftAP Somnium-Setup)
 creds ──► Associating ──timeout──► Backoff ──timer──► Associating
                │                       ▲
                └── associated          │ link lost
                       ▼                │
                    Online ─────────────┘
                       │
                       └── shutter+dial 1.5s ──► Portal
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

## HTTP (Wi-Fi camera or laptop)

`POST /v1/process` multipart: `image`, `style`, `taken_at`, optional `lat`/`lon`,
optional `source` (`wifi` or `usb`), optional `skip_gallery=1`.

The Wi-Fi camera sends `source=wifi`. The USB bridge sends `source=usb`.
`skip_gallery=1` means the camera will POST the orig/dream/meta trio itself
(bearer token from NVS, never from git) and the laptop hook should not.

Response: JPEG + headers `X-Somnium-Prompt`, `X-Somnium-Context`, …

`GET /health` includes `mdns` (`somnium.local`) and `upload` (`/v1/process`).
The server process advertises that name over mDNS unless `SOMNIUM_MDNS=0`.

Cooks retry connection loss and HTTP 408/429/5xx with backoff. They do not
queue a shot on the SD card. USB `COOK` / `DREAM` remains the fallback and is
unchanged.

## Config map

| Concern | File |
| --- | --- |
| Modes / prompts / theme / providers | `creative/creative.json` |
| Venue, API base URLs, model ids | `server/settings.json` |
| API key | `.env` → `XAI_KEY` |

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
```
