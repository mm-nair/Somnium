# Creative — single source of truth

Edit **`creative.json`** for look, modes, and which models to use. Then sync:

```sh
./scripts/sync_creative.sh
```

That regenerates firmware headers from this folder:

| You edit | Sync writes |
| --- | --- |
| `creative.json` → `theme`, `modes`, `ui.font` | `lib/SomniumApp/CreativeGen.h`, `Modes.h` |
| `logo.png` | `lib/SomniumApp/LogoBitmap.h` |

## Add a mode

1. Append to `modes` in `creative.json`:

```json
{
  "id": "vapor",
  "label": "VAPOR",
  "real": true,
  "tint": [255, 100, 200],
  "keywords": "pastel vaporwave grade",
  "prompt": "Apply a soft vaporwave COLOR GRADE only. Context mood: {context}."
}
```

2. `./scripts/sync_creative.sh`
3. Re-flash. Server picks up JSON on restart (no sync needed for Python).

## Change colors / font / logo

- **Colors** — `theme` RGB triples in `creative.json`
- **Font** — `ui.font` (Adafruit GFX font name, e.g. `FreeSerifBoldItalic12pt7b`), then sync
- **Logo** — replace `logo.png`, then sync

## Models

```json
"models": {
  "prompt_provider": "openai_compat",
  "image_provider": "xai",
  "context_provider": "openmeteo"
}
```

API host/keys live in `server/settings.json` + `.env` (`XAI_KEY`) — not here.
