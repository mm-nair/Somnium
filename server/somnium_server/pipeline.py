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

from somnium_server.devices import DeviceError
from somnium_server.ids import valid_id
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
        self.devices = cfg.get("_devices")

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
        device_id: str | None = None,
        board: str | None = None,
    ) -> ProcessResult:
        device_id = _clean_token(device_id, "device_id")
        board = _clean_token(board, "board")
        styles = self.cfg.get("styles") or {}
        requested = style_id
        location_label = None
        if self.devices is not None and (device_id or board):
            identity = self.devices.resolve(
                device_id=device_id,
                board=board,
                style_id=style_id,
                styles=styles,
                lat=lat,
                lon=lon,
            )
            device_id = identity.device_id
            board = identity.board
            style_id = identity.style_id
            lat = identity.lat
            lon = identity.lon
            location_label = identity.location_label
        if style_id not in styles:
            style_id = next(iter(styles), "popcomic")
        style = styles.get(style_id, {"label": style_id, "keywords": ""})

        location_cfg = dict(self.cfg.get("location") or {})
        if location_label:
            location_cfg["default_label"] = location_label
        context = self.stack["context"].enrich(
            taken_at=taken_at,
            client_ip=client_ip,
            lat=lat,
            lon=lon,
            location_cfg=location_cfg,
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
            prompt_cfg=_prompt_cfg_for(style, self.cfg.get("prompt") or {}),
        )
        image_prompt = _image_prompt(prompt, style)
        augmented = image_prompt != prompt

        use_real = force_real or bool(style.get("real"))
        if use_real:
            result_image = self.stack["image"].transform(
                image=image,
                prompt=image_prompt,
                image_cfg=_image_cfg_for(style, self.cfg.get("image") or {}),
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
        record: dict[str, Any] = {
            "style_id": style_id,
            "style_requested": requested,
            "prompt": prompt,
            "context": context,
            "taken_at": taken_at or context.get("taken_at"),
            "device_id": device_id,
            "board": board,
            "files": {
                "orig": orig_path.name,
                "dream": dream_path.name,
            },
        }
        if style.get("source"):
            record["style_source"] = style["source"]
        if style.get("negative"):
            record["negative"] = style["negative"]
        tuning = style.get("tuning") or {}
        if tuning:
            record["tuning"] = tuning
        if augmented:
            record["image_prompt"] = image_prompt
        meta_path.write_text(json.dumps(record, indent=2))
        # Optional gallery upload. Must not delay or fail the shot.
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
                "device_id": device_id,
                "board": board,
                "style_requested": requested,
            },
        )


def _clean_token(value: str | None, label: str) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    if not text:
        return None
    if not valid_id(text):
        raise DeviceError(f"invalid {label}")
    return text


def _prompt_cfg_for(style: dict[str, Any], base: dict[str, Any]) -> dict[str, Any]:
    prompt_cfg = dict(base)
    tuning = style.get("tuning") or {}
    if "temperature" in tuning:
        prompt_cfg["temperature"] = float(tuning["temperature"])
        prompt_cfg["temperature_locked"] = True
    if tuning.get("prompt_model"):
        prompt_cfg["model"] = tuning["prompt_model"]
    if "top_p" in tuning:
        prompt_cfg["top_p"] = float(tuning["top_p"])
    return prompt_cfg


def _image_cfg_for(style: dict[str, Any], base: dict[str, Any]) -> dict[str, Any]:
    image_cfg = dict(base)
    if style.get("liberal"):
        image_cfg["liberal"] = True
    tuning = style.get("tuning") or {}
    if tuning.get("image_model"):
        extra = dict(image_cfg.get("extra") or {})
        extra["model"] = tuning["image_model"]
        image_cfg["extra"] = extra
        image_cfg["model"] = tuning["image_model"]
    return image_cfg


def _image_prompt(prompt: str, style: dict[str, Any]) -> str:
    """Append per-style guidance the image model should see.

    grok-imagine-image edits has no public strength field, so strength is a
    sentence on the edit prompt. Built-in modes leave this equal to `prompt`.
    """
    tuning = style.get("tuning") or {}
    negative = style.get("negative") or tuning.get("negative") or ""
    lines: list[str] = []
    if negative and negative not in prompt:
        lines.append(f"Avoid: {negative}")
    if "strength" in tuning:
        lines.append(f"Stylization strength {float(tuning['strength']):.2f} on a 0 to 1 scale.")
    if not lines:
        return prompt
    return prompt.rstrip() + "\n" + "\n".join(lines)


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
