"""xAI Grok Imagine image edits (img2img)."""

from __future__ import annotations

import base64
import io
import json
import urllib.error
import urllib.request
from typing import Any

from somnium_server.config import env_secret


def _to_jpeg(raw: bytes) -> bytes:
    try:
        from PIL import Image
    except ImportError:
        return raw
    im = Image.open(io.BytesIO(raw))
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")
    elif im.mode == "L":
        im = im.convert("RGB")
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=90)
    return buf.getvalue()


class XaiImageProvider:
    """POST https://api.x.ai/v1/images/edits with a base64 data URI."""

    def transform(self, *, image: bytes, prompt: str, image_cfg: dict[str, Any]) -> bytes:
        key = env_secret(image_cfg.get("api_key_env", "XAI_KEY"))
        if not key:
            # Also accept XAI_API_KEY
            key = env_secret("XAI_API_KEY") or env_secret("XAI_KEY")
        if not key:
            raise RuntimeError("XAI_KEY missing — put it in repo .env")

        base = (image_cfg.get("base_url") or "https://api.x.ai/v1").rstrip("/")
        model = (image_cfg.get("extra") or {}).get("model") or image_cfg.get(
            "model", "grok-imagine-image-2.0"
        )
        b64 = base64.b64encode(image).decode()
        body: dict[str, Any] = {
            "model": model,
            "prompt": prompt,
            "image": {
                "url": f"data:image/jpeg;base64,{b64}",
                "type": "image_url",
            },
            "response_format": "b64_json",
        }
        # Optional knobs from settings.image_api.extra
        extra = dict(image_cfg.get("extra") or {})
        for k in ("aspect_ratio", "resolution", "quality", "n"):
            if k in extra:
                body[k] = extra[k]

        req = urllib.request.Request(
            f"{base}/images/edits",
            data=json.dumps(body).encode(),
            headers={
                "Authorization": f"Bearer {key}",
                "Content-Type": "application/json",
                "Accept": "application/json",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=240) as resp:
                payload = json.loads(resp.read().decode())
        except urllib.error.HTTPError as err:
            detail = err.read().decode(errors="replace")[:500]
            raise RuntimeError(f"xAI image edit HTTP {err.code}: {detail}") from err
        except urllib.error.URLError as err:
            raise RuntimeError(f"xAI image edit failed: {err}") from err

        data = payload.get("data") or []
        if not data:
            raise RuntimeError(f"xAI image edit empty data: {payload!r}"[:400])
        item = data[0]
        if item.get("b64_json"):
            return _to_jpeg(base64.b64decode(item["b64_json"]))
        if item.get("url"):
            with urllib.request.urlopen(item["url"], timeout=60) as img_resp:
                return _to_jpeg(img_resp.read())
        raise RuntimeError(f"xAI image edit missing b64/url: {list(item)}")
