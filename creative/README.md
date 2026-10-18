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
| `modes[].preview` → `previews/*.jpg` | `lib/SomniumApp/ModeBitmaps.h` |

Mode previews are compiled as 138x82 RGB565 thumbnails. Images retain their full
composition with black padding where needed. The carousel scales them inside
the existing image rectangles. A mode without `preview` uses its tint placeholder.

## Add a mode

1. Append to `modes` in `creative.json`:

```json
{
  "id": "vapor",
  "label": "VAPOR",
  "real": true,
  "liberal": false,
  "tint": [255, 100, 200],
  "keywords": "pastel vaporwave grade",
  "prompt": "Apply a soft vaporwave COLOR GRADE only. Context mood: {context}."
}
```

Set `"liberal": true` for styles that may stylize harder (e.g. anime) while still
keeping the same crop.

2. `./scripts/sync_creative.sh`
3. Re-flash. Server picks up JSON on restart (no sync needed for Python).

## Change colors / font / logo

- **Colors** — `theme` RGB triples in `creative.json`
- **Font** — `ui.font` (Adafruit GFX font name, e.g. `FreeSerifBoldItalic12pt7b`), then sync
- **Logo** — replace `logo.png`, then sync

## Mood → artifacts map

`mood_artifacts` picks **two** restrained lighting cues: weather and day/night. Temperature
and wind fill remaining slots when applicable. Edit pools freely; `pick` controls
the total count, and `always` entries are retained.

The prompt writer combines those cues into at most one short lighting sentence.
The medium, subject, and palette take priority. Cues adjust exposure, color
temperature, shadow softness, or reflected light on existing surfaces.
Astronomy remains metadata only. Both prompt paths use the selected `artifacts`
and never fall back to the raw sky summary or `mood_watermark` field.

SURPRISE uses `variations` to select a treatment, palette, and focus in code.
Each category excludes its previous choice within the running pipeline. The
selected values are saved in output metadata and used by both prompt providers.
PENCIL SKETCH uses full-color pencil strokes and colored shadows on ivory paper.

Prompts follow [xAI's image prompting guide](https://docs.x.ai/developers/model-capabilities/images/prompting-guide):
lead with the edit, describe physical materials and readable light, then preserve
the subject and layout. Image-provider constraints follow the requested edit.
All five style drafts describe their own rendering techniques. Preserve the
existing interior, sky, or ceiling; atmosphere is not permission to add scenery.

## Models

```json
"models": {
  "prompt_provider": "openai_compat",
  "image_provider": "xai",
  "context_provider": "openmeteo"
}
```

API host/keys live in `server/settings.json` + `.env` (`XAI_KEY`) — not here.
