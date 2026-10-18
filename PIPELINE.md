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
COOK style=<id> len=<n> lat=… lon=…
<n jpeg bytes>
COOK_END
```

Laptop → ESP:

```
DREAM <n>
<n jpeg bytes>
DREAM_END
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

## Camera-free cook

```sh
./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z
```
