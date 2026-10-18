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
        # Modes with real=false (default) just echo the capture — no model call.
        if style.get("real"):
            result_image = self.stack["image"].transform(
                image=image,
                prompt=prompt,
                image_cfg=self.cfg.get("image") or {},
            )
        else:
            result_image = image
            prompt = f"[placeholder/{style_id}] {prompt}"

        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        stem = self.output_dir / f"{stamp}_{style_id}"
        stem.with_suffix(".jpg").write_bytes(result_image)
        stem.with_name(stem.name + "_in.jpg").write_bytes(image)
        stem.with_suffix(".json").write_text(
            json.dumps(
                {
                    "style_id": style_id,
                    "prompt": prompt,
                    "context": context,
                },
                indent=2,
            )
        )

        return ProcessResult(
            image=result_image,
            prompt=prompt,
            context=context,
            style_id=style_id,
            meta={"saved_stem": str(stem)},
        )
