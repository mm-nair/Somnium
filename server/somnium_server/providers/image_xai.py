"""xAI Grok Imagine image edits — used as a style filter, output matched to input size."""

from __future__ import annotations

import base64
import io
import json
import time
import urllib.error
import urllib.request
from typing import Any

from somnium_server.config import env_secret

# The board abandons a cook at 300s. This provider is the long wait, and a
# URL fallback used to add another 60s on top of the edit call. Both steps
# share one budget so the JPEG is on the wire before the board gives up.
_IMAGE_BUDGET_S = 240.0

# Default: grade + subtle optical mood; no scene rewrite.
_FILTER_GUARD = (
    "Keep the source rendering and apply the requested color, light, and focus changes. "
    "Keep the same crop and recognizable subjects/layout. "
    "Apply the lighting finish only to existing surfaces, retaining the original light direction. "
    "Preserve the existing setting, including any sky or ceiling, and add no scenery or decorative elements. "
    "Do not add any readable text, logos, captions, or watermarks. "
)

# Surprise only. The other liberal styles stay on _LIBERAL_GUARD.
_SURPRISE_GUARD = (
    "Push this treatment as far as it can go. The same people stay recognizable, "
    "in the same pose, but materials, scale, light, and the room become the vision. "
    "Do not soften it into a tasteful filter or a polite photograph. "
    "No readable text, logos, captions, or watermarks. "
)

# Liberal styles (e.g. anime): stronger stylization, still same crop.
_LIBERAL_GUARD = (
    "Fully realize the requested medium, including photographic treatments. Keep the same crop "
    "and recognizable subjects/layout. Stylize the rendering of existing faces and materials. "
    "Preserve the existing setting, including any sky or ceiling, and add no scenery or decorative elements. "
    "Apply the lighting finish only to existing surfaces, retaining the original light direction. "
    "No readable text, logos, captions, or watermarks. "
)


def _jpeg_size(raw: bytes) -> tuple[int, int] | None:
    try:
        from PIL import Image
    except ImportError:
        return None
    with Image.open(io.BytesIO(raw)) as im:
        return im.size  # (w, h)


def _to_jpeg(raw: bytes, size: tuple[int, int] | None = None) -> bytes:
    try:
        from PIL import Image
    except ImportError:
        return raw
    im = Image.open(io.BytesIO(raw))
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")
    elif im.mode == "L":
        im = im.convert("RGB")
    if size is not None and im.size != size:
        im = im.resize(size, Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    # Panel is small; keep files light for SD + SPI.
    im.save(buf, format="JPEG", quality=85, optimize=True)
    return buf.getvalue()


class XaiImageProvider:
    """POST https://api.x.ai/v1/images/edits; downscale result to input pixels."""

    def transform(self, *, image: bytes, prompt: str, image_cfg: dict[str, Any]) -> bytes:
        key = env_secret(image_cfg.get("api_key_env", "XAI_KEY"))
        if not key:
            key = env_secret("XAI_API_KEY") or env_secret("XAI_KEY")
        if not key:
            raise RuntimeError("XAI_KEY missing — set XAI_KEY in the environment")

        in_size = _jpeg_size(image)
        base = (image_cfg.get("base_url") or "https://api.x.ai/v1").rstrip("/")
        model = (image_cfg.get("extra") or {}).get("model") or image_cfg.get(
            "model", "grok-imagine-image-2.0"
        )
        b64 = base64.b64encode(image).decode()
        if image_cfg.get("wild"):
            guard = _SURPRISE_GUARD
        elif image_cfg.get("liberal"):
            guard = _LIBERAL_GUARD
        else:
            guard = _FILTER_GUARD
        full_prompt = prompt.strip() + "\n\n" + guard

        body: dict[str, Any] = {
            "model": model,
            "prompt": full_prompt,
            "image": {
                "url": f"data:image/jpeg;base64,{b64}",
                "type": "image_url",
            },
            "response_format": "b64_json",
            # Prefer the source framing; we still resize to exact input pixels.
            "aspect_ratio": "auto",
        }
        extra = dict(image_cfg.get("extra") or {})
        for k in ("aspect_ratio", "resolution", "quality", "n"):
            if k in extra:
                body[k] = extra[k]
        # Default to cheapest/smallest gen; we resize locally to camera size.
        body.setdefault("resolution", "1k")
        body.setdefault("quality", "low")

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
        started = time.monotonic()
        try:
            with urllib.request.urlopen(req, timeout=_IMAGE_BUDGET_S) as resp:
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
            out = base64.b64decode(item["b64_json"])
        elif item.get("url"):
            remaining = _IMAGE_BUDGET_S - (time.monotonic() - started)
            if remaining < 1:
                raise RuntimeError("xAI image edit exceeded the cook budget")
            with urllib.request.urlopen(item["url"], timeout=remaining) as img_resp:
                out = img_resp.read()
        else:
            raise RuntimeError(f"xAI image edit missing b64/url: {list(item)}")

        return _to_jpeg(out, size=in_size)
