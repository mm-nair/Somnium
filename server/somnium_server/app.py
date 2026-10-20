from __future__ import annotations

import logging
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.responses import JSONResponse, Response

from somnium_server.config import load_config
from somnium_server.pipeline import Pipeline

cfg = load_config()
pipeline = Pipeline(cfg)
app = FastAPI(title="Somnium Server", version="0.2.0")
logger = logging.getLogger("uvicorn.error")


def form_flag(value: object) -> bool:
    # Direct calls (tests) leave the Form() default in place. Only a real
    # string field can ask the server to skip its gallery upload.
    if not isinstance(value, str):
        return False
    return value.strip().lower() in {"1", "true", "yes", "on"}


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


@app.post("/v1/process")
async def process(
    request: Request,
    image: UploadFile = File(...),
    style: str = Form("cartoon"),
    taken_at: Optional[str] = Form(None),
    lat: Optional[float] = Form(None),
    lon: Optional[float] = Form(None),
    force_real: Optional[str] = Form(None),
    skip_gallery: Optional[str] = Form(None),
):
    raw = await image.read()
    if len(raw) < 4 or raw[:2] != b"\xff\xd8":
        return JSONResponse({"error": "expected JPEG"}, status_code=400)

    client_ip = request.client.host if request.client else None
    force = form_flag(force_real)
    try:
        result = pipeline.process(
            image=raw,
            style_id=style,
            taken_at=taken_at,
            client_ip=client_ip,
            lat=lat,
            lon=lon,
            force_real=force,
            skip_gallery=form_flag(skip_gallery),
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
