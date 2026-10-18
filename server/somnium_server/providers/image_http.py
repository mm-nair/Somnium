from __future__ import annotations

import base64
import json
import urllib.error
import urllib.request
from typing import Any

from somnium_server.config import env_secret


class HttpImageProvider:
    """
    Generic HTTP img2img adapter. Map field names / extras in settings.json.

    Expected success response (first match wins):
      - raw image/* body
      - JSON { "image": "<base64>" } or { "b64_json": "..." } or { "url": "..." }
    """

    def transform(self, *, image: bytes, prompt: str, image_cfg: dict[str, Any]) -> bytes:
        endpoint = image_cfg.get("endpoint")
        if not endpoint:
            raise RuntimeError("image.endpoint missing in config")

        headers = {"Accept": "application/json, image/*"}
        key = env_secret(image_cfg.get("api_key_env", ""))
        if key:
            headers["Authorization"] = f"Bearer {key}"

        image_field = image_cfg.get("image_field", "image")
        prompt_field = image_cfg.get("prompt_field", "prompt")
        extra = dict(image_cfg.get("extra") or {})
        body_mode = image_cfg.get("body_mode", "json")

        if body_mode == "json":
            payload = {
                **extra,
                prompt_field: prompt,
                image_field: base64.b64encode(image).decode(),
            }
            data = json.dumps(payload).encode()
            headers["Content-Type"] = "application/json"
        else:
            # Minimal multipart (enough for many APIs; extend per-provider later).
            boundary = "----SomniumBoundary7MA4YWxkTrZu0gW"
            chunks: list[bytes] = []
            for key_name, value in {**extra, prompt_field: prompt}.items():
                chunks.append(f"--{boundary}\r\n".encode())
                chunks.append(
                    f'Content-Disposition: form-data; name="{key_name}"\r\n\r\n'.encode()
                )
                chunks.append(str(value).encode())
                chunks.append(b"\r\n")
            chunks.append(f"--{boundary}\r\n".encode())
            chunks.append(
                (
                    f'Content-Disposition: form-data; name="{image_field}"; '
                    f'filename="capture.jpg"\r\n'
                    f"Content-Type: image/jpeg\r\n\r\n"
                ).encode()
            )
            chunks.append(image)
            chunks.append(b"\r\n")
            chunks.append(f"--{boundary}--\r\n".encode())
            data = b"".join(chunks)
            headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"

        req = urllib.request.Request(
            endpoint,
            data=data,
            headers=headers,
            method=image_cfg.get("method", "POST"),
        )
        try:
            with urllib.request.urlopen(req, timeout=180) as resp:
                content_type = resp.headers.get("Content-Type", "")
                raw = resp.read()
        except urllib.error.URLError as err:
            raise RuntimeError(f"image provider HTTP failed: {err}") from err

        if content_type.startswith("image/"):
            return raw
        try:
            payload = json.loads(raw.decode())
        except (UnicodeDecodeError, json.JSONDecodeError) as err:
            raise RuntimeError("image provider returned undecodable body") from err

        for key_name in ("image", "b64_json", "b64"):
            if key_name in payload and isinstance(payload[key_name], str):
                return base64.b64decode(payload[key_name])
        if "url" in payload:
            with urllib.request.urlopen(payload["url"], timeout=60) as img_resp:
                return img_resp.read()
        raise RuntimeError(f"Unrecognized image provider JSON keys: {list(payload)}")
