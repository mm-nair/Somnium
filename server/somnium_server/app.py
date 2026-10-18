from __future__ import annotations

import logging
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.responses import JSONResponse, Response

from somnium_server.config import load_config
from somnium_server.devices import DeviceError
from somnium_server.ids import valid_id
from somnium_server.pipeline import Pipeline
from somnium_server.shots import boards_with_shots, list_shots

cfg = load_config()
pipeline = Pipeline(cfg)
app = FastAPI(title="Somnium Server", version="0.3.0")
logger = logging.getLogger("uvicorn.error")


def header_preview(text: str, limit: int = 512) -> str:
    text = text.encode("ascii", "replace").decode("ascii")
    text = "".join(character if 32 <= ord(character) <= 126 else " " for character in text)
    return " ".join(text.split())[:limit].strip()


def _as_optional_str(value: object) -> str | None:
    """Form defaults are Form objects when the route is called directly."""
    if not isinstance(value, str):
        return None
    text = value.strip()
    return text or None


def _request_header(request: Request, name: str) -> str | None:
    """Missing header lists show up when tests call the route function directly."""
    scope = getattr(request, "scope", None) or {}
    if not scope.get("headers"):
        return None
    return request.headers.get(name)


def _optional_id(value: object, header: str | None, label: str) -> str | None:
    text = _as_optional_str(value) or _as_optional_str(header)
    if text is None:
        return None
    if not valid_id(text):
        raise DeviceError(f"invalid {label}")
    return text


@app.get("/health")
def health():
    registry = cfg.get("_style_registry")
    devices = cfg.get("_devices")
    return {
        "ok": True,
        "prompt_provider": cfg.get("prompt", {}).get("provider"),
        "image_provider": cfg.get("image", {}).get("provider"),
        "context_provider": cfg.get("context", {}).get("provider"),
        "styles": list((cfg.get("styles") or {}).keys()),
        "creative": cfg.get("_creative_path"),
        "style_problems": list(getattr(registry, "problems", []) or []),
        "devices": len(devices.list_devices()) if devices is not None else 0,
    }


@app.post("/v1/process")
async def process(
    request: Request,
    image: UploadFile = File(...),
    style: str = Form("popcomic"),
    taken_at: Optional[str] = Form(None),
    lat: Optional[float] = Form(None),
    lon: Optional[float] = Form(None),
    force_real: Optional[str] = Form(None),
    device_id: Optional[str] = Form(None),
    board: Optional[str] = Form(None),
):
    raw = await image.read()
    if len(raw) < 4 or raw[:2] != b"\xff\xd8":
        return JSONResponse({"error": "expected JPEG"}, status_code=400)

    client_ip = request.client.host if request.client else None
    force_text = force_real if isinstance(force_real, str) else ""
    force = force_text.lower() in ("1", "true", "yes", "on")
    try:
        device = _optional_id(
            device_id, _request_header(request, "x-somnium-device"), "device_id"
        )
        board_id = _optional_id(board, _request_header(request, "x-somnium-board"), "board")
        result = pipeline.process(
            image=raw,
            style_id=style if isinstance(style, str) else "popcomic",
            taken_at=taken_at if isinstance(taken_at, str) else None,
            client_ip=client_ip,
            lat=lat if isinstance(lat, (int, float)) else None,
            lon=lon if isinstance(lon, (int, float)) else None,
            force_real=force,
            device_id=device,
            board=board_id,
        )
    except DeviceError as err:
        return JSONResponse({"error": str(err)}, status_code=400)
    except Exception as err:  # noqa: BLE001 — edge client gets a clean error
        logger.exception("Processing failed for style=%s", style)
        return JSONResponse({"error": str(err)}, status_code=502)

    headers = {
        "X-Somnium-Style": header_preview(result.style_id),
        "X-Somnium-Prompt": header_preview(result.prompt),
        "X-Somnium-Context": header_preview(result.context.get("summary", ""), 256),
        "X-Somnium-Orig": header_preview(Path(result.meta.get("orig_path", "")).name),
        "X-Somnium-Dream": header_preview(Path(result.meta.get("dream_path", "")).name),
    }
    if result.meta.get("device_id"):
        headers["X-Somnium-Device"] = header_preview(str(result.meta["device_id"]))
    if result.meta.get("board"):
        headers["X-Somnium-Board"] = header_preview(str(result.meta["board"]))
    return Response(content=result.image, media_type="image/jpeg", headers=headers)


@app.get("/v1/styles")
def list_styles():
    registry = cfg.get("_style_registry")
    if registry is None:
        styles = [
            {"id": style_id, **(payload if isinstance(payload, dict) else {})}
            for style_id, payload in (cfg.get("styles") or {}).items()
        ]
        return {"styles": styles, "problems": []}
    return {
        "styles": [style.public_dict() for style in registry.all_styles()],
        "problems": list(registry.problems),
    }


@app.get("/v1/styles/{style_id}")
def get_style(style_id: str):
    registry = cfg.get("_style_registry")
    if registry is None:
        payload = (cfg.get("styles") or {}).get(style_id)
        if payload is None:
            return JSONResponse({"error": "unknown style"}, status_code=404)
        return {"id": style_id, **payload}
    style = registry.get(style_id)
    if style is None:
        return JSONResponse({"error": "unknown style"}, status_code=404)
    return style.public_dict()


@app.post("/v1/styles/reload")
def reload_styles():
    registry = cfg.get("_style_registry")
    if registry is None:
        return JSONResponse({"error": "style registry is not configured"}, status_code=501)
    registry.reload()
    fresh = registry.as_map()
    current = cfg.get("styles")
    if isinstance(current, dict):
        current.clear()
        current.update(fresh)
    else:
        cfg["styles"] = fresh
    return {"ok": True, "styles": list(fresh), "problems": list(registry.problems)}


@app.get("/v1/devices")
def list_devices():
    devices = cfg.get("_devices")
    if devices is None:
        return {"devices": []}
    return {"devices": devices.list_devices()}


@app.get("/v1/devices/{device_id}")
def get_device(device_id: str):
    devices = cfg.get("_devices")
    if devices is None:
        return JSONResponse({"error": "devices are not configured"}, status_code=501)
    try:
        record = devices.get_device(device_id)
    except DeviceError as err:
        return JSONResponse({"error": str(err)}, status_code=400)
    if record is None:
        return JSONResponse({"error": "unknown device"}, status_code=404)
    return record


@app.post("/v1/devices")
async def register_device(request: Request):
    devices = cfg.get("_devices")
    if devices is None:
        return JSONResponse({"error": "devices are not configured"}, status_code=501)
    try:
        payload = await request.json()
    except Exception:  # noqa: BLE001
        return JSONResponse({"error": "expected JSON"}, status_code=400)
    if not isinstance(payload, dict):
        return JSONResponse({"error": "expected JSON object"}, status_code=400)
    device_id = payload.get("id") or payload.get("device_id")
    try:
        record, created = devices.register(str(device_id or ""), payload)
    except DeviceError as err:
        return JSONResponse({"error": str(err)}, status_code=400)
    return JSONResponse(record, status_code=201 if created else 200)


@app.patch("/v1/devices/{device_id}")
async def update_device(device_id: str, request: Request):
    devices = cfg.get("_devices")
    if devices is None:
        return JSONResponse({"error": "devices are not configured"}, status_code=501)
    try:
        payload = await request.json()
    except Exception:  # noqa: BLE001
        return JSONResponse({"error": "expected JSON"}, status_code=400)
    if not isinstance(payload, dict):
        return JSONResponse({"error": "expected JSON object"}, status_code=400)
    try:
        return devices.update(device_id, payload)
    except DeviceError as err:
        status = 404 if str(err).startswith("unknown device") else 400
        return JSONResponse({"error": str(err)}, status_code=status)


@app.get("/v1/boards")
def list_boards():
    devices = cfg.get("_devices")
    declared = devices.list_boards() if devices is not None else []
    shots = list_shots(pipeline.output_dir)
    return {"boards": boards_with_shots(declared, shots)}


@app.post("/v1/boards")
async def create_board(request: Request):
    devices = cfg.get("_devices")
    if devices is None:
        return JSONResponse({"error": "devices are not configured"}, status_code=501)
    try:
        payload = await request.json()
    except Exception:  # noqa: BLE001
        return JSONResponse({"error": "expected JSON"}, status_code=400)
    if not isinstance(payload, dict):
        return JSONResponse({"error": "expected JSON object"}, status_code=400)
    try:
        record = devices.create_board(
            str(payload.get("id") or payload.get("board") or ""),
            name=payload.get("name"),
            members=payload.get("members") or [],
        )
    except DeviceError as err:
        status = 409 if "already exists" in str(err) else 400
        return JSONResponse({"error": str(err)}, status_code=status)
    return JSONResponse(record, status_code=201)


@app.get("/v1/boards/{board_id}")
def get_board(board_id: str):
    devices = cfg.get("_devices")
    declared = []
    if devices is not None:
        try:
            record = devices.get_board(board_id)
        except DeviceError as err:
            return JSONResponse({"error": str(err)}, status_code=400)
        if record is not None:
            declared = [record]
    shots = list_shots(pipeline.output_dir, board=board_id)
    merged = boards_with_shots(declared, shots)
    if not merged:
        return JSONResponse({"error": "unknown board"}, status_code=404)
    body = merged[0]
    body["shots"] = shots
    return body


@app.get("/v1/boards/{board_id}/shots")
def list_board_shots(board_id: str):
    if not valid_id(board_id):
        return JSONResponse({"error": "invalid board"}, status_code=400)
    devices = cfg.get("_devices")
    known = False
    if devices is not None:
        try:
            known = devices.get_board(board_id) is not None
        except DeviceError as err:
            return JSONResponse({"error": str(err)}, status_code=400)
    shots = list_shots(pipeline.output_dir, board=board_id)
    if not known and not shots:
        return JSONResponse({"error": "unknown board"}, status_code=404)
    return {"board": board_id, "shots": shots}


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
