#!/usr/bin/env python3
"""Camera-free Somnium cook — PNG/JPEG + style + timestamp through the filter.

Uses the same pipeline as the ESP (context → xAI prompt → xAI image edit).

  nix develop
  ./scripts/cook.sh process --style anime
  ./scripts/cook.sh process/nyc.png --style anime --out-dir proc_output
"""

from __future__ import annotations

import argparse
import io
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".webp"}


def _iso_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def http_base_from_env() -> str:
    raw = os.environ.get("SOMNIUM_SERVER_URL", "").strip().rstrip("/")
    if not raw:
        return "http://127.0.0.1:8080"
    suffix = "/v1/process"
    if raw.endswith(suffix):
        raw = raw[: -len(suffix)]
    return raw or "http://127.0.0.1:8080"


def _to_jpeg(raw: bytes) -> bytes:
    if raw[:2] == b"\xff\xd8":
        return raw
    from PIL import Image

    im = Image.open(io.BytesIO(raw))
    if im.mode not in ("RGB", "L"):
        im = im.convert("RGB")
    elif im.mode == "L":
        im = im.convert("RGB")
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=92, optimize=True)
    return buf.getvalue()


def _collect_images(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    if not path.is_dir():
        return []
    return sorted(
        p
        for p in path.iterdir()
        if p.is_file() and p.suffix.lower() in IMAGE_EXTS
    )


def cook_local(
    image_path: Path,
    style: str,
    taken_at: str,
    lat: float | None,
    lon: float | None,
    force_real: bool,
    out_dir: Path,
) -> int:
    sys.path.insert(0, str(ROOT / "server"))
    from somnium_server.config import load_config
    from somnium_server.pipeline import Pipeline

    cfg = load_config()
    pipe = Pipeline(cfg)
    try:
        raw = _to_jpeg(image_path.read_bytes())
    except Exception as err:  # noqa: BLE001 — surface bad image clearly
        print(f"error: cannot read {image_path.name}: {err}", file=sys.stderr)
        return 1

    print(f"cooking {image_path.name} style={style} at={taken_at} …")
    result = pipe.process(
        image=raw,
        style_id=style,
        taken_at=taken_at,
        client_ip="cook-local",
        lat=lat,
        lon=lon,
        force_real=force_real,
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    stem = image_path.stem
    orig_out = out_dir / f"{stem}_orig.jpg"
    dream_out = out_dir / f"{stem}_dream.jpg"
    orig_out.write_bytes(result.original)
    dream_out.write_bytes(result.image)

    print("context:", result.context.get("summary"))
    print("prompt:", result.prompt)
    print("orig: ", orig_out)
    print("dream:", dream_out)
    return 0


def cook_http(
    image_path: Path,
    style: str,
    taken_at: str,
    lat: float | None,
    lon: float | None,
    force_real: bool,
    base_url: str,
    out_dir: Path,
) -> int:
    import urllib.error
    import urllib.request

    boundary = "----SomniumCookBoundary"
    fields: list[tuple[str, str]] = [
        ("style", style),
        ("taken_at", taken_at),
    ]
    if lat is not None:
        fields.append(("lat", str(lat)))
    if lon is not None:
        fields.append(("lon", str(lon)))
    if force_real:
        fields.append(("force_real", "1"))

    try:
        raw = _to_jpeg(image_path.read_bytes())
    except Exception as err:  # noqa: BLE001
        print(f"error: cannot read {image_path.name}: {err}", file=sys.stderr)
        return 1

    chunks: list[bytes] = []
    for name, value in fields:
        chunks.append(f"--{boundary}\r\n".encode())
        chunks.append(
            f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode()
        )
        chunks.append(value.encode())
        chunks.append(b"\r\n")
    chunks.append(f"--{boundary}\r\n".encode())
    chunks.append(
        (
            'Content-Disposition: form-data; name="image"; '
            f'filename="{image_path.stem}.jpg"\r\n'
            "Content-Type: image/jpeg\r\n\r\n"
        ).encode()
    )
    chunks.append(raw)
    chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode())
    body = b"".join(chunks)

    url = base_url.rstrip("/") + "/v1/process"
    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
    token = os.environ.get("SOMNIUM_PROCESS_TOKEN", "").strip()
    if token:
        headers["X-Somnium-Token"] = token
    req = urllib.request.Request(
        url,
        data=body,
        headers=headers,
        method="POST",
    )
    print(f"POST {url} style={style} at={taken_at} …")
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            data = resp.read()
            headers = {k.lower(): v for k, v in resp.headers.items()}
    except urllib.error.HTTPError as err:
        print(err.read().decode(errors="replace"), file=sys.stderr)
        return 1
    except urllib.error.URLError as err:
        print(f"error: {err} — is ./scripts/server.sh running?", file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    orig_out = out_dir / f"{image_path.stem}_orig.jpg"
    dream_out = out_dir / f"{image_path.stem}_dream.jpg"
    orig_out.write_bytes(raw)
    dream_out.write_bytes(data)
    print("prompt:", headers.get("x-somnium-prompt", ""))
    print("context:", headers.get("x-somnium-context", ""))
    print("orig: ", orig_out)
    print("dream:", dream_out)
    return 0


def main() -> int:
    sys.path.insert(0, str(ROOT / "server"))
    from somnium_server.config import load_dotenv

    load_dotenv()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "image",
        type=Path,
        help="Image file or folder (e.g. process/)",
    )
    p.add_argument("--style", default="anime", help="Mode id from creative.json")
    p.add_argument(
        "--at",
        dest="taken_at",
        default=None,
        help="ISO-8601 UTC timestamp (default: now)",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=ROOT / "proc_output",
        help="Where to write {name}_orig.jpg / {name}_dream.jpg",
    )
    p.add_argument("--lat", type=float, default=None)
    p.add_argument("--lon", type=float, default=None)
    p.add_argument(
        "--local",
        action="store_true",
        help="Run pipeline in-process (no HTTP server)",
    )
    p.add_argument(
        "--http",
        default=None,
        help="Server base URL when not using --local (default: local, or SOMNIUM_SERVER_URL)",
    )
    p.add_argument(
        "--force-real",
        action="store_true",
        help="Force model call even if style.real is false",
    )
    args = p.parse_args()
    images = _collect_images(args.image)
    if not images:
        print(f"error: no images in {args.image}", file=sys.stderr)
        return 1

    taken_at = args.taken_at or _iso_now()
    base_url = args.http or http_base_from_env()
    out_dir = args.out_dir if args.out_dir.is_absolute() else ROOT / args.out_dir
    force_real = args.force_real or True
    rc = 0
    for image in images:
        if args.local:
            code = cook_local(
                image,
                args.style,
                taken_at,
                args.lat,
                args.lon,
                force_real,
                out_dir,
            )
        else:
            code = cook_http(
                image,
                args.style,
                taken_at,
                args.lat,
                args.lon,
                force_real,
                base_url,
                out_dir,
            )
        if code != 0:
            rc = code
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
