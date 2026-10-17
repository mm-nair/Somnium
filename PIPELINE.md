# Pipeline contract (revertable)

Edge (ESP32 C++) and brain (laptop Python) talk over one HTTP call.
Either side can be replaced without rewriting the other.

## Request

`POST /v1/process` `multipart/form-data`

| Field | Required | Notes |
| --- | --- | --- |
| `image` | yes | JPEG |
| `style` | no | `cartoon` / `cyberpunk` / … (see `server/config.yaml`) |
| `taken_at` | no | ISO-8601 UTC from NTP |
| `lat` / `lon` | no | optional overrides |

## Response

- `200` `image/jpeg` body (result to show + save)
- Headers (optional metadata): `X-Somnium-Prompt`, `X-Somnium-Context`, `X-Somnium-Style`
- Errors: JSON `{"error":"..."}` with 4xx/5xx

## Responsibility split

| Concern | Where | Revert |
| --- | --- | --- |
| Capture, LCD, buttons, SD, Wi-Fi | ESP `lib/Somnium` | always |
| HTTP upload / download | ESP `ApiClient` | disable with `SOMNIUM_USE_SERVER 0` |
| Context, prompt eng, models | `server/` | delete/ignore; camera still shoots locally |
| Prompts & provider choice | `server/config.yaml` | edit YAML only |

## Swap models

1. Implement `providers/image_*.py` or `prompt_*.py`
2. Register in `providers/registry.py`
3. Set `image.provider` / `prompt.provider` in `config.yaml`

No firmware flash needed for prompt/model changes.
