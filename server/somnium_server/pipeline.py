from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from somnium_server.providers.registry import build_stack


@dataclass
class ProcessResult:
    image: bytes
    original: bytes
    prompt: str
    context: dict[str, Any]
    style_id: str
    meta: dict[str, Any] = field(default_factory=dict)


class Pipeline:
    def __init__(self, cfg: dict[str, Any]):
        self.cfg = cfg
        self.stack = build_stack(cfg)
        out = Path(cfg.get("server", {}).get("output_dir", "outputs"))
        if not out.is_absolute():
            out = Path(__file__).resolve().parent.parent / out
        out.mkdir(parents=True, exist_ok=True)
        self.output_dir = out

    def process(
        self,
        *,
        image: bytes,
        style_id: str,
        taken_at: str | None,
        client_ip: str | None,
        lat: float | None = None,
        lon: float | None = None,
        force_real: bool = False,
    ) -> ProcessResult:
        styles = self.cfg.get("styles") or {}
        if style_id not in styles:
            style_id = next(iter(styles), "cartoon")
        style = styles.get(style_id, {"label": style_id, "keywords": ""})

        context = self.stack["context"].enrich(
            taken_at=taken_at,
            client_ip=client_ip,
            lat=lat,
            lon=lon,
            location_cfg=self.cfg.get("location") or {},
            context_cfg=self.cfg.get("context") or {},
        )
        prompt = self.stack["prompt"].synthesize(
            style_id=style_id,
            style=style,
            context=context,
            prompt_cfg=self.cfg.get("prompt") or {},
        )

        use_real = force_real or bool(style.get("real"))
        if use_real:
            result_image = self.stack["image"].transform(
                image=image,
                prompt=prompt,
                image_cfg=self.cfg.get("image") or {},
            )
        else:
            result_image = image
            prompt = f"[placeholder/{style_id}] {prompt}"

        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        if taken_at:
            try:
                stamp = (
                    datetime.fromisoformat(taken_at.replace("Z", "+00:00"))
                    .astimezone(timezone.utc)
                    .strftime("%Y%m%dT%H%M%SZ")
                )
            except ValueError:
                pass

        # Dual slideshow entries: …_0_orig then …_1_dream (dial alternates).
        stem = f"{stamp}_{style_id}"
        orig_path = self.output_dir / f"{stem}_0_orig.jpg"
        dream_path = self.output_dir / f"{stem}_1_dream.jpg"
        meta_path = self.output_dir / f"{stem}.json"
        orig_path.write_bytes(image)
        dream_path.write_bytes(result_image)
        meta_path.write_text(
            json.dumps(
                {
                    "style_id": style_id,
                    "prompt": prompt,
                    "context": context,
                    "taken_at": taken_at or context.get("taken_at"),
                    "files": {
                        "orig": orig_path.name,
                        "dream": dream_path.name,
                    },
                },
                indent=2,
            )
        )

        return ProcessResult(
            image=result_image,
            original=image,
            prompt=prompt,
            context=context,
            style_id=style_id,
            meta={
                "saved_stem": stem,
                "orig_path": str(orig_path),
                "dream_path": str(dream_path),
            },
        )
