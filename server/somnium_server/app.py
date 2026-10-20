from __future__ import annotations

import logging
import tempfile
from pathlib import Path
from typing import Any, Optional

from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.params import Form as FormMarker
from fastapi.responses import JSONResponse, Response

from somnium_server.config import load_config
from somnium_server.device_state import (
    DeviceStatusStore,
    ShotLedger,
    normalize_telemetry,
    shot_identity,
)
from somnium_server.pipeline import Pipeline

cfg = load_config()
pipeline = Pipeline(cfg)
app = FastAPI(title="Somnium Server", version="0.3.0")
logger = logging.getLogger("uvicorn.error")


def _state_dir(name: str) -> Path:
    out = getattr(pipeline, "output_dir", None)
    if isinstance(out, Path):
        return out / name
    return Path(tempfile.gettempdir()) / "somnium-runtime" / name


ledger = ShotLedger(_state_dir("ledger"))
status_store = DeviceStatusStore(_state_dir("device_status.json"))


def _given(value: Any) -> Any:
    """Direct unit calls pass Form(...) defaults through; HTTP does not."""
    if isinstance(value, FormMarker):
        return None
    return value


def header_preview(text: str, limit: int = 512) -> str:
    text = text.encode("ascii", "replace").decode("ascii")
    text = "".join(character if 32 <= ord(character) <= 126 else " " for character in text)
    return " ".join(text.split())[:limit].strip()


@app.get("/health")
def health():
    return {
        "ok": True,
        "prompt_provider": cfg.get("prompt", {}).get("provider"),
        "image_provider": cfg.get("image", {}).get("provider"),
        "context_provider": cfg.get("context", {}).get("provider"),
        "styles": list((cfg.get("styles") or {}).keys()),
        "creative": cfg.get("_creative_path"),
    }


def _shot_headers(
    *,
    style_id: str,
    prompt: str,
    summary: str,
    orig_name: str,
    dream_name: str,
    sequence_id: str | None = None,
    taken_at: str | None = None,
    idempotent: bool = False,
) -> dict[str, str]:
    headers = {
        "X-Somnium-Style": header_preview(style_id),
        "X-Somnium-Prompt": header_preview(prompt),
        "X-Somnium-Context": header_preview(summary, 256),
        "X-Somnium-Orig": header_preview(orig_name),
        "X-Somnium-Dream": header_preview(dream_name),
    }
    if sequence_id:
        headers["X-Somnium-Sequence"] = header_preview(sequence_id, 64)
    if taken_at:
        headers["X-Somnium-Taken-At"] = header_preview(taken_at, 64)
    if idempotent:
        headers["X-Somnium-Idempotent"] = "replay"
    return headers


def _record_shot_telemetry(
    *,
    device_id: str | None,
    sequence_id: str | None,
    queue_id: str | None,
    battery_pct: float | None,
    battery_mv: int | None,
    charging: int | None,
    queue_depth: int | None,
    low_battery: int | None,
) -> None:
    if all(
        value is None
        for value in (
            battery_pct,
            battery_mv,
            charging,
            queue_depth,
            low_battery,
            sequence_id,
            queue_id,
        )
    ):
        return
    fields = normalize_telemetry(
        {
            "device_id": device_id,
            "sequence_id": sequence_id,
            "queue_id": queue_id,
            "battery_pct": battery_pct,
            "battery_mv": battery_mv,
            "charging": charging,
            "queue_depth": queue_depth,
            "low_battery": low_battery,
        },
        source="shot",
        default_device=device_id or "unknown",
    )
    status_store.update(fields)


def apply_status_payload(payload: dict[str, Any]) -> dict[str, Any]:
    fields = normalize_telemetry(payload, source="status")
    device = status_store.update(fields)
    return {"ok": True, "device": device}


@app.get("/api/device/status")
def device_status(device_id: Optional[str] = None):
    return status_store.snapshot(device_id)


@app.post("/api/device/status")
async def device_status_post(request: Request):
    try:
        payload = await request.json()
    except Exception:  # noqa: BLE001 — bad body is a client error
        return JSONResponse({"error": "expected json"}, status_code=400)
    if not isinstance(payload, dict):
        return JSONResponse({"error": "expected object"}, status_code=400)
    return apply_status_payload(payload)


@app.post("/v1/process")
async def process(
    request: Request,
    image: UploadFile = File(...),
    style: str = Form("cartoon"),
    taken_at: Optional[str] = Form(None),
    lat: Optional[float] = Form(None),
    lon: Optional[float] = Form(None),
    force_real: Optional[str] = Form(None),
    device_id: Optional[str] = Form(None),
    sequence_id: Optional[str] = Form(None),
    queue_id: Optional[str] = Form(None),
    battery_pct: Optional[float] = Form(None),
    battery_mv: Optional[int] = Form(None),
    charging: Optional[int] = Form(None),
    queue_depth: Optional[int] = Form(None),
    low_battery: Optional[int] = Form(None),
):
    raw = await image.read()
    if len(raw) < 4 or raw[:2] != b"\xff\xd8":
        return JSONResponse({"error": "expected JPEG"}, status_code=400)

    taken_at = _given(taken_at)
    lat = _given(lat)
    lon = _given(lon)
    force_real = _given(force_real)
    device_id = _given(device_id)
    sequence_id = _given(sequence_id)
    queue_id = _given(queue_id)
    battery_pct = _given(battery_pct)
    battery_mv = _given(battery_mv)
    charging = _given(charging)
    queue_depth = _given(queue_depth)
    low_battery = _given(low_battery)

    client_ip = request.client.host if request.client else None
    force = (force_real or "").lower() in ("1", "true", "yes", "on")
    device, sequence = shot_identity(device_id, sequence_id, queue_id)
    _record_shot_telemetry(
        device_id=device if sequence or device_id else None,
        sequence_id=sequence,
        queue_id=queue_id,
        battery_pct=battery_pct,
        battery_mv=battery_mv,
        charging=charging,
        queue_depth=queue_depth,
        low_battery=low_battery,
    )
    if sequence:
        cached = ledger.lookup(device, sequence)
        if cached is not None:
            meta = cached.meta
            logger.info("Idempotent replay device=%s sequence=%s", device, sequence)
            return Response(
                content=cached.image,
                media_type="image/jpeg",
                headers=_shot_headers(
                    style_id=str(meta.get("style_id") or style),
                    prompt=str(meta.get("prompt") or ""),
                    summary=str(meta.get("summary") or ""),
                    orig_name=str(meta.get("orig_name") or ""),
                    dream_name=str(meta.get("dream_name") or ""),
                    sequence_id=sequence,
                    taken_at=str(meta.get("taken_at") or taken_at or ""),
                    idempotent=True,
                ),
            )
    try:
        result = pipeline.process(
            image=raw,
            style_id=style,
            taken_at=taken_at,
            client_ip=client_ip,
            lat=lat,
            lon=lon,
            force_real=force,
            device_id=device if sequence or device_id else None,
            sequence_id=sequence,
            queue_id=queue_id or sequence,
            battery_pct=battery_pct,
            battery_mv=battery_mv,
            charging=charging,
            queue_depth=queue_depth,
        )
    except Exception as err:  # noqa: BLE001 — edge client gets a clean error
        logger.exception("Processing failed for style=%s", style)
        return JSONResponse({"error": str(err)}, status_code=502)

    orig_name = Path(result.meta.get("orig_path", "")).name
    dream_name = Path(result.meta.get("dream_path", "")).name
    if sequence:
        dream_path = result.meta.get("dream_path")
        if dream_path:
            ledger.remember(
                device,
                sequence,
                dream_path=Path(dream_path),
                meta={
                    "style_id": result.style_id,
                    "prompt": result.prompt,
                    "summary": result.context.get("summary", ""),
                    "taken_at": taken_at or result.context.get("taken_at"),
                    "orig_name": orig_name,
                    "dream_name": dream_name,
                    "queue_id": queue_id or sequence,
                },
            )
    return Response(
        content=result.image,
        media_type="image/jpeg",
        headers=_shot_headers(
            style_id=result.style_id,
            prompt=result.prompt,
            summary=result.context.get("summary", ""),
            orig_name=orig_name,
            dream_name=dream_name,
            sequence_id=sequence,
            taken_at=taken_at or result.context.get("taken_at"),
        ),
    )


def main():
    import uvicorn

    server = cfg.get("server") or {}
    uvicorn.run(
        "somnium_server.app:app",
        host=server.get("host", "0.0.0.0"),
        port=int(server.get("port", 8080)),
        reload=False,
    )


if __name__ == "__main__":
    main()
