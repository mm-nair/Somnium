# Style engine

Presets used to be the `modes` array in `creative/creative.json`, copied into a dict at startup. That file is still the source for the five camera modes. The server now loads those modes into a style registry, then adds anything an operator drops in beside them.

## What a style is

A style is a JSON object. The schema is `somnium_server/styles/style.schema.json`. The checker that actually runs is `somnium_server/styles/schema.py` (no extra package).

Required: `id` (`^[a-z][a-z0-9_-]{0,31}$`).

Optional: `label`, `keywords`, `prompt`, `real`, `liberal`, `enabled`, `variations`, `preview`, `tint`, `negative`, and `tuning`.

`tuning` may set:

| Field | Effect |
| --- | --- |
| `temperature` | Prompt-model temperature. When set, the liberal +0.15 bump is not applied. |
| `prompt_model` | Overrides the chat model for this style. |
| `top_p` | Sent on the chat request only when set. |
| `image_model` | Overrides the image edit model for this style. |
| `strength` | 0 to 1. Written into the image prompt. The edits endpoint has no strength parameter. |
| `negative` | Same as the top-level `negative` field. Appended to the image prompt as `Avoid: …` when the draft does not already contain it. |

`preview` is a relative path (no `..`). The camera carousel still uses the bitmap compiled by `scripts/sync_creative.sh`. The server preview is for API clients.

`real: false` keeps the old placeholder path: the original JPEG is returned and the prompt is prefixed with `[placeholder/<id>]`.

## Template variables

Built-in drafts only use `{context}` and, for Surprise, `{variation}`.

`{context}` is the lighting finish: the mood-artifact lines joined together, or `retain the existing lighting` when there are none. It is not the raw weather summary and it does not include the venue name. That behavior is unchanged.

User styles may also use:

| Variable | Value |
| --- | --- |
| `place`, `location_label` | Device or venue label |
| `weather` | Weather summary |
| `day_part` | `day` or `night` |
| `temp_c`, `wind` | Numbers from the context provider |
| `moon` | Moon phase name |
| `style_id`, `style_label`, `style_keywords` | The style |
| `negative` | The style's negative guidance |
| `taken_at`, `lat`, `lon` | Shot metadata |

`{place}` and `{moon}` are opt-in. The built-in modes leave them out so the image model does not paint the venue or the sky into the frame.

## Load order

Later entries replace the same id:

1. `creative/creative.json` modes
2. `server/styles/user/*.json` (or `styles_dir` in settings)
3. `user_styles` in `server/settings.json`
4. Python modules listed in `style_plugins`

A broken user file is skipped. `GET /health` and `GET /v1/styles` include `problems`. A broken built-in mode still stops startup.

`server/styles/examples/cyanotype.json` is a sample. It is not loaded until it is copied into `styles/user/` or the plugin is listed:

```json
"style_plugins": ["somnium_server.styles.plugins.cyanotype"]
```

Then restart, or `POST /v1/styles/reload`.

A plugin module exposes `register(registry)` or a `STYLE` dict. `register` wins when both exist.

## HTTP

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/v1/styles` | Every style, including disabled ones, plus load problems |
| GET | `/v1/styles/{id}` | One style, with its prompt and variable names |
| POST | `/v1/styles/reload` | Re-read the styles directory and plugins |

`GET /health` still lists enabled style ids under `styles`.

## Not done

- Dropping a JSON file does not put that mode on the camera carousel. That still needs `sync_creative.sh`, a rebuild, and a flash. On-camera UI is owned by another track.
- Reload is a POST, not a file watcher.
- `strength` is prompt text, not an xAI request field.
