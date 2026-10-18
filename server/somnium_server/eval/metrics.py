"""Compare an original JPEG with its dream.

The scores are scene-preservation checks, not a beauty contest. A restyle may
change color and still keep the same shapes. Identity should score near 1.
A scrambled or blank frame should fail.

Metrics, all computed locally with Pillow:

* structural similarity on coarse grayscale blocks
* correlation of edge maps
* average-hash similarity
* color-histogram intersection
"""

from __future__ import annotations

import io
import math
from dataclasses import dataclass, field
from typing import Any


DEFAULT_THRESHOLDS = {
    "ssim_min": 0.45,
    "edge_min": 0.35,
    "ahash_min": 0.55,
}

STRICT_THRESHOLDS = {
    "ssim_min": 0.98,
    "edge_min": 0.98,
    "ahash_min": 0.98,
}


@dataclass
class SceneScore:
    ssim: float
    edge: float
    ahash: float
    histogram: float
    passed: bool
    thresholds: dict[str, float] = field(default_factory=dict)
    notes: list[str] = field(default_factory=list)

    def as_dict(self) -> dict[str, Any]:
        return {
            "ssim": round(self.ssim, 4),
            "edge": round(self.edge, 4),
            "ahash": round(self.ahash, 4),
            "histogram": round(self.histogram, 4),
            "passed": self.passed,
            "thresholds": self.thresholds,
            "notes": self.notes,
        }


def compare_images(
    original: bytes,
    dream: bytes,
    *,
    thresholds: dict[str, float] | None = None,
) -> SceneScore:
    limits = dict(DEFAULT_THRESHOLDS)
    if thresholds:
        limits.update(thresholds)
    gray_a = _gray(original)
    gray_b = _gray(dream, size=(len(gray_a[0]), len(gray_a)))
    ssim = _block_ssim(gray_a, gray_b)
    edge = _pearson(_edges(original), _edges(dream))
    ahash = _ahash_similarity(original, dream)
    histogram = _histogram_intersection(original, dream)
    notes: list[str] = []
    passed = True
    for name, value, key in (
        ("ssim", ssim, "ssim_min"),
        ("edge", edge, "edge_min"),
        ("ahash", ahash, "ahash_min"),
    ):
        if value < limits[key]:
            passed = False
            notes.append(f"{name} {value:.3f} is below {limits[key]:.3f}")
    return SceneScore(
        ssim=ssim,
        edge=edge,
        ahash=ahash,
        histogram=histogram,
        passed=passed,
        thresholds=limits,
        notes=notes,
    )


def _open(raw: bytes):
    from PIL import Image

    try:
        return Image.open(io.BytesIO(raw))
    except Exception as err:  # noqa: BLE001 — surface a short message to the CLI
        raise ValueError(f"could not read image: {err}") from err


def _gray(raw: bytes, size: tuple[int, int] = (64, 48)) -> list[list[int]]:
    image = _open(raw).convert("L").resize(size, _resample())
    pixels = list(image.tobytes())
    width, height = size
    return [pixels[y * width : (y + 1) * width] for y in range(height)]


def _resample():
    from PIL import Image

    return Image.Resampling.BILINEAR


def _block_ssim(a: list[list[int]], b: list[list[int]], window: int = 8) -> float:
    height = min(len(a), len(b))
    width = min(len(a[0]), len(b[0])) if height else 0
    scores: list[float] = []
    for top in range(0, height - window + 1, window):
        for left in range(0, width - window + 1, window):
            block_a = [
                a[y][x]
                for y in range(top, top + window)
                for x in range(left, left + window)
            ]
            block_b = [
                b[y][x]
                for y in range(top, top + window)
                for x in range(left, left + window)
            ]
            scores.append(_ssim_window(block_a, block_b))
    if not scores:
        return _ssim_window(
            [pixel for row in a for pixel in row],
            [pixel for row in b for pixel in row],
        )
    return sum(scores) / len(scores)


def _ssim_window(a: list[int], b: list[int]) -> float:
    count = len(a)
    if count == 0 or count != len(b):
        return 0.0
    mean_a = sum(a) / count
    mean_b = sum(b) / count
    var_a = sum((pixel - mean_a) ** 2 for pixel in a) / count
    var_b = sum((pixel - mean_b) ** 2 for pixel in b) / count
    cov = sum((x - mean_a) * (y - mean_b) for x, y in zip(a, b)) / count
    c1 = (0.01 * 255) ** 2
    c2 = (0.03 * 255) ** 2
    numerator = (2 * mean_a * mean_b + c1) * (2 * cov + c2)
    denominator = (mean_a ** 2 + mean_b ** 2 + c1) * (var_a + var_b + c2)
    if denominator == 0:
        return 1.0
    return max(-1.0, min(1.0, numerator / denominator))


def _edges(raw: bytes, size: tuple[int, int] = (64, 48)) -> list[int]:
    from PIL import ImageFilter

    image = _open(raw).convert("L").resize(size, _resample())
    return list(image.filter(ImageFilter.FIND_EDGES).tobytes())


def _pearson(a: list[int], b: list[int]) -> float:
    count = min(len(a), len(b))
    if count == 0:
        return 0.0
    a = a[:count]
    b = b[:count]
    mean_a = sum(a) / count
    mean_b = sum(b) / count
    var_a = sum((pixel - mean_a) ** 2 for pixel in a)
    var_b = sum((pixel - mean_b) ** 2 for pixel in b)
    if var_a == 0 and var_b == 0:
        return 1.0
    if var_a == 0 or var_b == 0:
        return 0.0
    cov = sum((x - mean_a) * (y - mean_b) for x, y in zip(a, b))
    return max(-1.0, min(1.0, cov / math.sqrt(var_a * var_b)))


def _ahash(raw: bytes) -> list[int]:
    image = _open(raw).convert("L").resize((8, 8), _resample())
    pixels = list(image.tobytes())
    average = sum(pixels) / len(pixels)
    return [1 if pixel >= average else 0 for pixel in pixels]


def _ahash_similarity(a: bytes, b: bytes) -> float:
    left = _ahash(a)
    right = _ahash(b)
    matches = sum(bit_a == bit_b for bit_a, bit_b in zip(left, right))
    return matches / len(left)


def _histogram_intersection(a: bytes, b: bytes, bins: int = 8) -> float:
    return sum(min(x, y) for x, y in zip(_histogram(a, bins), _histogram(b, bins)))


def _histogram(raw: bytes, bins: int) -> list[float]:
    image = _open(raw).convert("RGB").resize((64, 48), _resample())
    counts = [0] * (bins * 3)
    samples = image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()
    for red, green, blue in samples:
        counts[min(bins - 1, red * bins // 256)] += 1
        counts[bins + min(bins - 1, green * bins // 256)] += 1
        counts[2 * bins + min(bins - 1, blue * bins // 256)] += 1
    total = float(sum(counts) or 1)
    return [count / total for count in counts]
