from __future__ import annotations

import logging
import os
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.responses import JSONResponse, Response

from somnium_server.auth import ensure_auth_config, process_token, rejection
from somnium_server.config import load_config
from somnium_server.pipeline import Pipeline

ensure_auth_config()
cfg = load_config()
pipeline = Pipeline(cfg)
logger = logging.getLogger("uvicorn.error")
_docs = None if process_token() else "/docs"
app = FastAPI(
    title="Somnium Server",
    version="0.2.1",
    docs_url=_docs,
    redoc_url=None if process_token() else "/redoc",
    openapi_url=None if process_token() else "/openapi.json",
)


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
        "auth": bool(process_token()),
    }


@app.middleware("http")
async def require_process_auth(request: Request, call_next):
    if request.method == "POST" and request.url.path == "/v1/process":
        denied = rejection(request)
        if denied is not None:
            logger.warning("rejected POST /v1/process: missing or wrong process token")
            return denied
    return await call_next(request)


@app.post("/v1/process")
async def process(
    request: Request,
    image: UploadFile = File(...),
    style: str = Form("cartoon"),
    taken_at: Optional[str] = Form(None),
    lat: Optional[float] = Form(None),
    lon: Optional[float] = Form(None),
    force_real: Optional[str] = Form(None),
):
    denied = rejection(request)
    if denied is not None:
        logger.warning("rejected POST /v1/process: missing or wrong process token")
        return denied

    raw = await image.read()
    if len(raw) < 4 or raw[:2] != b"\xff\xd8":
        return JSONResponse({"error": "expected JPEG"}, status_code=400)

    client_ip = request.client.host if request.client else None
    force = (force_real or "").lower() in ("1", "true", "yes", "on")
    try:
        result = pipeline.process(
            image=raw,
            style_id=style,
            taken_at=taken_at,
            client_ip=client_ip,
            lat=lat,
            lon=lon,
            force_real=force,
        )
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
    return Response(content=result.image, media_type="image/jpeg", headers=headers)


def main():
    import inspect
    import uvicorn

    ensure_auth_config()
    if not process_token():
        logger.warning("SOMNIUM_PROCESS_TOKEN unset; POST /v1/process is not authenticated")
    server = cfg.get("server") or {}
    host = os.environ.get("SOMNIUM_HOST", "").strip() or server.get("host", "0.0.0.0")
    port = int(os.environ.get("SOMNIUM_PORT") or os.environ.get("PORT") or server.get("port", 8080))
    kwargs = {}
    if "timeout_graceful_shutdown" in inspect.signature(uvicorn.run).parameters:
        kwargs["timeout_graceful_shutdown"] = 25
    uvicorn.run(
        "somnium_server.app:app",
        host=host,
        port=port,
        reload=False,
        **kwargs,
    )


if __name__ == "__main__":
    main()
