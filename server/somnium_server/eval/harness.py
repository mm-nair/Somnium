"""Run a cook and score how much of the scene survived.

CI uses ``MockImagine``, which tints pixels and never calls the network.
Live mode is off unless ``SOMNIUM_EVAL_LIVE`` is set.
"""

from __future__ import annotations

import io
import os
from pathlib import Path
from typing import Any

from somnium_server.eval.metrics import DEFAULT_THRESHOLDS, SceneScore, compare_images


class LiveEvalDisabled(RuntimeError):
    """Raised when live mode is requested without SOMNIUM_EVAL_LIVE."""


def live_enabled() -> bool:
    return os.environ.get("SOMNIUM_EVAL_LIVE", "").strip().lower() in {
        "1",
        "true",
        "yes",
        "on",
    }


class MockImagine:
    """Stand-in for grok-imagine-image. Keeps geometry, shifts color."""

    def __init__(self):
        self.prompts: list[str] = []
        self.calls = 0

    def transform(self, *, image: bytes, prompt: str, image_cfg: dict[str, Any]) -> bytes:
        del image_cfg
        self.calls += 1
        self.prompts.append(prompt)
        return restyle_jpeg(image)


def restyle_jpeg(raw: bytes) -> bytes:
    from PIL import Image

    image = Image.open(io.BytesIO(raw)).convert("RGB")
    red, green, blue = image.split()
    red = red.point(lambda value: min(255, int(value * 1.04 + 6)))
    green = green.point(lambda value: min(255, int(value * 0.98)))
    blue = blue.point(lambda value: max(0, min(255, int(value * 0.90))))
    graded = Image.merge("RGB", (red, green, blue))
    buffer = io.BytesIO()
    graded.save(buffer, format="JPEG", quality=90)
    return buffer.getvalue()


def solid_jpeg(
    size: tuple[int, int] = (160, 120), color: tuple[int, int, int] = (220, 30, 30)
) -> bytes:
    from PIL import Image

    image = Image.new("RGB", size, color)
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=90)
    return buffer.getvalue()


def scramble_jpeg(raw: bytes, seed: int = 1) -> bytes:
    import random

    from PIL import Image

    image = Image.open(io.BytesIO(raw)).convert("RGB")
    pixels = list(image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata())
    random.Random(seed).shuffle(pixels)
    image.putdata(pixels)
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=90)
    return buffer.getvalue()


def scene_jpeg(size: tuple[int, int] = (160, 120)) -> bytes:
    """A small drawn scene with a stable subject and background."""
    from PIL import Image, ImageDraw

    image = Image.new("RGB", size, (236, 226, 206))
    draw = ImageDraw.Draw(image)
    draw.rectangle((18, 28, 110, 100), fill=(40, 70, 120))
    draw.ellipse((48, 40, 88, 80), fill=(240, 200, 150))
    draw.rectangle((0, 100, size[0], size[1]), fill=(90, 110, 70))
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=92)
    return buffer.getvalue()


def evaluate_pair(
    original: bytes,
    dream: bytes,
    *,
    thresholds: dict[str, float] | None = None,
) -> SceneScore:
    return compare_images(original, dream, thresholds=thresholds or DEFAULT_THRESHOLDS)


def evaluate_pipeline(
    pipeline,
    image: bytes,
    *,
    style_id: str,
    taken_at: str = "2026-10-17T19:30:00Z",
    thresholds: dict[str, float] | None = None,
    **kwargs: Any,
) -> dict[str, Any]:
    result = pipeline.process(
        image=image,
        style_id=style_id,
        taken_at=taken_at,
        client_ip="eval",
        **kwargs,
    )
    score = evaluate_pair(result.original, result.image, thresholds=thresholds)
    return {
        "style_id": result.style_id,
        "prompt": result.prompt,
        "score": score.as_dict(),
        "stem": result.meta.get("saved_stem"),
    }


def run_live(image_path: Path, style_id: str, *, taken_at: str | None = None) -> dict[str, Any]:
    """Cook one file with the real providers. Requires SOMNIUM_EVAL_LIVE."""
    if not live_enabled():
        raise LiveEvalDisabled(
            "live eval is off. Set SOMNIUM_EVAL_LIVE=1 to call the image API."
        )
    from somnium_server.config import load_config
    from somnium_server.pipeline import Pipeline

    cfg = load_config()
    pipeline = Pipeline(cfg)
    return evaluate_pipeline(
        pipeline,
        image_path.read_bytes(),
        style_id=style_id,
        taken_at=taken_at or "2026-10-17T19:30:00Z",
        force_real=True,
    )
