from __future__ import annotations

import json
import logging
import os
import random
import threading
import urllib.error
import urllib.request
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from somnium_server.mood_artifacts import apply_mood_artifacts
from somnium_server.providers.registry import build_stack

logger = logging.getLogger("uvicorn.error")
_UPLOAD_TIMEOUT_S = 20


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
        self.previous_variations: dict[str, dict[str, str]] = {}
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
        source: str | None = None,
        skip_gallery: bool = False,
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
        # Mood watermark → concrete optical artifacts (editable in creative.json).
        context = apply_mood_artifacts(
            context,
            style_id=style_id,
            creative=self.cfg.get("creative") or {},
        )
        variation: dict[str, str] = {}
        previous_variation = self.previous_variations.get(style_id, {})
        for category, options in (style.get("variations") or {}).items():
            candidates = [
                option for option in options
                if option != previous_variation.get(category)
            ] or options
            if candidates:
                variation[category] = random.SystemRandom().choice(candidates)
        if variation:
            self.previous_variations[style_id] = variation
            context["variation"] = variation
        prompt = self.stack["prompt"].synthesize(
            style_id=style_id,
            style=style,
            context=context,
            prompt_cfg=self.cfg.get("prompt") or {},
        )

        use_real = force_real or bool(style.get("real"))
        if use_real:
            image_cfg = dict(self.cfg.get("image") or {})
            if style.get("liberal"):
                image_cfg["liberal"] = True
            if style_id == "surprise":
                image_cfg["wild"] = True
            result_image = self.stack["image"].transform(
                image=image,
                prompt=prompt,
                image_cfg=image_cfg,
            )
        else:
            result_image = image
            prompt = f"[placeholder/{style_id}] {prompt}"

        # Filename uses server wall-clock so rapid shots never clobber each other
        # (client taken_at is often second-granularity and can collide).
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%f")[:17]
        stem = f"{stamp}_{style_id}"
        orig_path = self.output_dir / f"{stem}_0_orig.jpg"
        dream_path = self.output_dir / f"{stem}_1_dream.jpg"
        meta_path = self.output_dir / f"{stem}.json"
        n = 0
        while orig_path.exists() or dream_path.exists():
            n += 1
            stem = f"{stamp}_{style_id}_{n}"
            orig_path = self.output_dir / f"{stem}_0_orig.jpg"
            dream_path = self.output_dir / f"{stem}_1_dream.jpg"
            meta_path = self.output_dir / f"{stem}.json"

        # Dual slideshow on the ThinkPad: …_0_orig then …_1_dream.
        orig_path.write_bytes(image)
        dream_path.write_bytes(result_image)
        meta_path.write_text(
            json.dumps(
                {
                    "style_id": style_id,
                    "prompt": prompt,
                    "context": context,
                    "taken_at": taken_at or context.get("taken_at"),
                    "source": source,
                    "files": {
                        "orig": orig_path.name,
                        "dream": dream_path.name,
                    },
                },
                indent=2,
            )
        )
        # Optional gallery upload. Must not delay or fail the shot.
        # The camera sends skip_gallery when it will POST the pair itself.
        if skip_gallery:
            logger.info("Gallery upload left to the camera for %s", stem)
        else:
            _start_gallery_upload(
                orig_path=orig_path,
                dream_path=dream_path,
                meta_path=meta_path,
                stem=stem,
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


def _start_gallery_upload(
    *,
    orig_path: Path,
    dream_path: Path,
    meta_path: Path,
    stem: str,
) -> None:
    """POST the saved shot when both upload env vars are set.

    Runs on a daemon thread. Missing config is a no-op. Network and HTTP
    failures are logged and never propagate to the caller.
    """
    url = os.environ.get("SOMNIUM_UPLOAD_URL", "").strip()
    token = os.environ.get("SOMNIUM_UPLOAD_TOKEN", "").strip()
    if not url or not token:
        return

    def _run() -> None:
        try:
            _post_gallery_upload(
                url=url,
                token=token,
                orig_path=orig_path,
                dream_path=dream_path,
                meta_path=meta_path,
                stem=stem,
            )
        except Exception:
            logger.exception("Gallery upload failed for %s", stem)

    try:
        threading.Thread(
            target=_run,
            name=f"somnium-gallery-upload-{stem}",
            daemon=True,
        ).start()
    except Exception:
        logger.exception("Gallery upload failed to start for %s", stem)


def _post_gallery_upload(
    *,
    url: str,
    token: str,
    orig_path: Path,
    dream_path: Path,
    meta_path: Path,
    stem: str,
) -> None:
    body, content_type = _encode_multipart(
        [
            ("orig", orig_path.name, "image/jpeg", orig_path.read_bytes()),
            ("dream", dream_path.name, "image/jpeg", dream_path.read_bytes()),
            ("meta", f"{stem}.json", "application/json", meta_path.read_bytes()),
        ]
    )
    request = urllib.request.Request(
        url,
        data=body,
        headers={
            "Content-Type": content_type,
            "X-Somnium-Token": token,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=_UPLOAD_TIMEOUT_S) as response:
            status = getattr(response, "status", None)
            response.read()
    except urllib.error.HTTPError as err:
        err.close()
        raise
    logger.info("Gallery upload succeeded for %s (HTTP %s)", stem, status)


def _encode_multipart(parts: list[tuple[str, str, str, bytes]]) -> tuple[bytes, str]:
    """Build a multipart/form-data body. parts are (name, filename, content_type, data)."""
    payload = b"".join(data for _, _, _, data in parts)
    boundary = "----SomniumUpload" + uuid.uuid4().hex
    while boundary.encode() in payload:
        boundary = "----SomniumUpload" + uuid.uuid4().hex
    chunks: list[bytes] = []
    for name, filename, content_type, data in parts:
        chunks.append(f"--{boundary}\r\n".encode())
        chunks.append(
            (
                f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
                f"Content-Type: {content_type}\r\n\r\n"
            ).encode()
        )
        chunks.append(data)
        chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode())
    return b"".join(chunks), f"multipart/form-data; boundary={boundary}"
