#!/usr/bin/env python3
"""Camera-free Somnium cook — upload a test JPEG + style + timestamp.

Uses the same pipeline as the ESP (context → xAI prompt → xAI image edit)
and writes dual slideshow entries: *_0_orig.jpg and *_1_dream.jpg.

  nix develop
  # start server in another terminal: ./scripts/server.sh
  ./scripts/cook.sh photo.jpg --style noir --at 2026-10-17T19:30:00Z

  # Or hit the pipeline in-process (no HTTP server needed):
  python tools/cook.py photo.jpg --style noir --at 2026-10-17T19:30:00Z --local
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _iso_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def cook_local(
    image_path: Path,
    style: str,
    taken_at: str,
    lat: float | None,
    lon: float | None,
    force_real: bool,
) -> int:
    sys.path.insert(0, str(ROOT / "server"))
    from somnium_server.config import load_config
    from somnium_server.pipeline import Pipeline

    cfg = load_config()
    pipe = Pipeline(cfg)
    raw = image_path.read_bytes()
    if raw[:2] != b"\xff\xd8":
        print("error: expected JPEG", file=sys.stderr)
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
    print("context:", result.context.get("summary"))
    print("prompt:", result.prompt)
    print("orig: ", result.meta.get("orig_path"))
    print("dream:", result.meta.get("dream_path"))
    return 0


def cook_http(
    image_path: Path,
    style: str,
    taken_at: str,
    lat: float | None,
    lon: float | None,
    force_real: bool,
    base_url: str,
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

    raw = image_path.read_bytes()
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
            f'filename="{image_path.name}"\r\n'
            "Content-Type: image/jpeg\r\n\r\n"
        ).encode()
    )
    chunks.append(raw)
    chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode())
    body = b"".join(chunks)

    url = base_url.rstrip("/") + "/v1/process"
    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
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

    out = ROOT / "server" / "outputs" / f"cook_{style}_dream.jpg"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print("prompt:", headers.get("x-somnium-prompt", ""))
    print("context:", headers.get("x-somnium-context", ""))
    print("dream saved:", out)
    print("server also wrote dual entries under server/outputs/")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("image", type=Path, help="Test JPEG path")
    p.add_argument("--style", default="noir", help="Mode id from creative.json")
    p.add_argument(
        "--at",
        dest="taken_at",
        default=None,
        help="ISO-8601 UTC timestamp (default: now)",
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
        default="http://127.0.0.1:8080",
        help="Server base URL when not using --local",
    )
    p.add_argument(
        "--force-real",
        action="store_true",
        help="Force model call even if style.real is false",
    )
    args = p.parse_args()
    if not args.image.is_file():
        print(f"error: missing image {args.image}", file=sys.stderr)
        return 1
    taken_at = args.taken_at or _iso_now()
    if args.local:
        return cook_local(
            args.image,
            args.style,
            taken_at,
            args.lat,
            args.lon,
            args.force_real or True,
        )
    return cook_http(
        args.image,
        args.style,
        taken_at,
        args.lat,
        args.lon,
        args.force_real or True,
        args.http,
    )


if __name__ == "__main__":
    raise SystemExit(main())
