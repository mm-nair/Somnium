# Creative config

Edit **`creative.json`** for modes / prompts:

- `modes[].prompt` — img2img prompt text (`{context}` filled in)
- `modes[].real` — `false` (default) echoes the photo; `true` calls the image model
- `models.image_provider` — used only when `real` is true (`echo` | `http` | …)

Plumbing: `server/settings.json`. Camera labels: keep `lib/SomniumApp/Modes.h` in sync.
