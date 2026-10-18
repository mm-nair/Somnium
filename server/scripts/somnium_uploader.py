#!/usr/bin/env python3
"""Upload Somnium camera shots to the dream board.

The FastAPI pipeline writes three files per shot into its output directory:

    <stem>_0_orig.jpg
    <stem>_1_dream.jpg
    <stem>.json

`upload_shot` posts that trio to the gallery's `/api/upload` route. Watch mode
polls an outputs directory, so the Somnium repo does not need to change.
`delete` removes one shot with DELETE <upload-url>?id=<id>.

Environment (laptop):
    SOMNIUM_UPLOAD_URL    https://<your-domain>/api/upload
    SOMNIUM_UPLOAD_TOKEN  same secret as the Vercel SOMNIUM_UPLOAD_TOKEN

    python3 scripts/somnium_uploader.py watch /path/to/Somnium/server/outputs
    python3 scripts/somnium_uploader.py upload ORIG.jpg DREAM.jpg META.json
    python3 scripts/somnium_uploader.py delete SHOT_ID

Windows, with the py launcher:

    py scripts/somnium_uploader.py watch C:\\path\\to\\Somnium\\server\\outputs
    py scripts/somnium_uploader.py upload ORIG.jpg DREAM.jpg META.json
    py scripts/somnium_uploader.py delete SHOT_ID
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

MAX_TOTAL_BYTES = 4_200_000
STATE_NAME = ".somnium-uploaded.json"
SETTLE_SECONDS = 1.5
TOO_LARGE_LIMIT = 3


class UploadError(Exception):
    def __init__(self, message: str, status: int | None = None):
        super().__init__(message)
        self.status = status


def _credentials(url, token):
    endpoint = (url or os.environ.get("SOMNIUM_UPLOAD_URL") or "").strip()
    secret = (token or os.environ.get("SOMNIUM_UPLOAD_TOKEN") or "").strip()
    if not endpoint:
        raise UploadError("SOMNIUM_UPLOAD_URL is not set")
    if not secret:
        raise UploadError("SOMNIUM_UPLOAD_TOKEN is not set")
    return endpoint, secret


def _open_json(request, endpoint, timeout):
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = response.read().decode()
    except urllib.error.HTTPError as err:
        detail = err.read().decode(errors="replace")
        message = _error_message(detail) or err.reason or "upload failed"
        raise UploadError(f"{err.code} {message}", err.code) from err
    except urllib.error.URLError as err:
        raise UploadError(f"could not reach {endpoint}: {err.reason}") from err
    try:
        parsed = json.loads(payload)
    except json.JSONDecodeError as err:
        raise UploadError("upload response was not JSON") from err
    if not isinstance(parsed, dict) or not parsed.get("ok"):
        raise UploadError("upload response was not ok")
    return parsed


def upload_shot(orig_path, dream_path, meta_path, *, url=None, token=None, timeout=60):
    """POST one finished shot. Returns the gallery JSON (`ok` and `photo`)."""
    orig = Path(orig_path)
    dream = Path(dream_path)
    meta = Path(meta_path)
    endpoint, secret = _credentials(url, token)
    for path, label in ((orig, "orig"), (dream, "dream"), (meta, "meta")):
        if not path.is_file():
            raise UploadError(f"{label} file does not exist: {path}")
    orig_bytes = orig.read_bytes()
    dream_bytes = dream.read_bytes()
    meta_bytes = meta.read_bytes()
    for label, data in (("orig", orig_bytes), ("dream", dream_bytes)):
        if len(data) < 2 or data[0:2] != b"\xff\xd8":
            raise UploadError(f"{label} must be a JPEG")
    total = len(orig_bytes) + len(dream_bytes) + len(meta_bytes)
    if total > MAX_TOTAL_BYTES:
        raise UploadError(
            f"shot is {total} bytes; the upload function accepts about 4.5 MB per request"
        )
    boundary = "somnium" + uuid.uuid4().hex
    body = _multipart(
        boundary,
        (
            ("orig", orig.name, "image/jpeg", orig_bytes),
            ("dream", dream.name, "image/jpeg", dream_bytes),
            ("meta", meta.name, "application/json", meta_bytes),
        ),
    )
    request = urllib.request.Request(endpoint, data=body, method="POST")
    request.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    request.add_header("X-Somnium-Token", secret)
    request.add_header("User-Agent", "somnium-uploader/1")
    return _open_json(request, endpoint, timeout)


def delete_shot(shot_id, *, url=None, token=None, timeout=60):
    """DELETE one shot. Returns the gallery JSON (`ok` and `deleted`)."""
    endpoint, secret = _credentials(url, token)
    shot = str(shot_id or "").strip()
    if not shot:
        raise UploadError("shot id is required")
    parts = urllib.parse.urlsplit(endpoint)
    query = dict(urllib.parse.parse_qsl(parts.query, keep_blank_values=True))
    query["id"] = shot
    target = urllib.parse.urlunsplit((parts.scheme, parts.netloc, parts.path, urllib.parse.urlencode(query), parts.fragment))
    request = urllib.request.Request(target, method="DELETE")
    request.add_header("X-Somnium-Token", secret)
    request.add_header("User-Agent", "somnium-uploader/1")
    return _open_json(request, target, timeout)


def watch(output_dir, *, interval=2.0, once=False, url=None, token=None):
    """Upload every complete shot in `output_dir`, then keep watching unless once."""
    root = Path(output_dir)
    if not root.is_dir():
        raise UploadError(f"not a directory: {root}")
    state_path = root / STATE_NAME
    state = _load_state(state_path)

    def scan():
        uploaded = 0
        for meta_path in _meta_paths(root):
            shot = _resolve_shot(meta_path)
            if shot is None:
                continue
            orig, dream = shot
            if _still_settling(orig, dream, meta_path):
                continue
            if _too_large_count(state, meta_path.stem) >= TOO_LARGE_LIMIT:
                continue
            key = _fingerprint(orig, dream, meta_path)
            if state.setdefault("shots", {}).get(meta_path.stem) == key:
                continue
            try:
                upload_shot(orig, dream, meta_path, url=url, token=token)
            except UploadError as err:
                if err.status == 413:
                    count = _note_too_large(state, meta_path.stem)
                    _save_state(state_path, state)
                    if count >= TOO_LARGE_LIMIT:
                        print(
                            f"not retrying {meta_path.stem}: upload returned 413 three times",
                            file=sys.stderr,
                            flush=True,
                        )
                print(f"upload failed {meta_path.stem}: {err}", file=sys.stderr, flush=True)
                continue
            state["shots"][meta_path.stem] = key
            state.get("too_large", {}).pop(meta_path.stem, None)
            _save_state(state_path, state)
            uploaded += 1
            print(f"uploaded {meta_path.stem}", flush=True)
        return uploaded

    if once:
        return scan()
    while True:
        scan()
        time.sleep(interval)


def _meta_paths(root: Path):
    for path in sorted(root.glob("*")):
        if not path.is_file():
            continue
        if path.name == STATE_NAME or path.name.endswith(".tmp"):
            continue
        if path.suffix.lower() != ".json":
            continue
        yield path


def _still_settling(*paths: Path) -> bool:
    newest = max(path.stat().st_mtime for path in paths)
    return time.time() - newest < SETTLE_SECONDS


def _too_large_count(state: dict, stem: str) -> int:
    counts = state.get("too_large")
    if not isinstance(counts, dict):
        return 0
    try:
        return int(counts.get(stem, 0))
    except (TypeError, ValueError):
        return 0


def _note_too_large(state: dict, stem: str) -> int:
    counts = state.setdefault("too_large", {})
    if not isinstance(counts, dict):
        counts = {}
        state["too_large"] = counts
    count = _too_large_count(state, stem) + 1
    counts[stem] = count
    return count


def _resolve_shot(meta_path: Path):
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(meta, dict):
        return None
    files = meta.get("files") if isinstance(meta.get("files"), dict) else {}
    folder = meta_path.parent
    orig = folder / files["orig"] if files.get("orig") else folder / f"{meta_path.stem}_0_orig.jpg"
    dream = folder / files["dream"] if files.get("dream") else folder / f"{meta_path.stem}_1_dream.jpg"
    if not orig.is_file() or not dream.is_file():
        return None
    if orig.stat().st_size == 0 or dream.stat().st_size == 0:
        return None
    return orig, dream


def _fingerprint(orig: Path, dream: Path, meta: Path) -> str:
    parts = []
    for path in (orig, dream, meta):
        stat = path.stat()
        parts.append(f"{stat.st_size}:{stat.st_mtime_ns}")
    return "|".join(parts)


def _load_state(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {"shots": {}}
    if not isinstance(data, dict):
        return {"shots": {}}
    shots = data.get("shots")
    if not isinstance(shots, dict):
        data["shots"] = {}
    return data


def _save_state(path: Path, state: dict) -> None:
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(state, indent=2))
    tmp.replace(path)


def _multipart(boundary: str, parts) -> bytes:
    chunks = []
    for name, filename, content_type, data in parts:
        chunks.append(
            (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
                f"Content-Type: {content_type}\r\n\r\n"
            ).encode()
        )
        chunks.append(data)
        chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode())
    return b"".join(chunks)


def _error_message(detail: str) -> str:
    try:
        parsed = json.loads(detail)
    except json.JSONDecodeError:
        return detail.strip()[:300]
    if isinstance(parsed, dict) and parsed.get("error"):
        return str(parsed["error"])
    return detail.strip()[:300]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Upload Somnium shots to the dream board")
    parser.add_argument("--url", help="Override SOMNIUM_UPLOAD_URL")
    parser.add_argument("--token", help="Override SOMNIUM_UPLOAD_TOKEN")
    sub = parser.add_subparsers(dest="cmd", required=True)

    upload = sub.add_parser("upload", help="Upload one orig + dream + meta trio")
    upload.add_argument("orig")
    upload.add_argument("dream")
    upload.add_argument("meta")

    folder = sub.add_parser("watch", help="Watch an outputs directory and upload new shots")
    folder.add_argument("output_dir")
    folder.add_argument("--interval", type=float, default=2.0, help="Seconds between scans")
    folder.add_argument("--once", action="store_true", help="Upload current shots and exit")

    remove = sub.add_parser("delete", help="Delete one shot by id")
    remove.add_argument("id")

    args = parser.parse_args(argv)
    try:
        if args.cmd == "upload":
            result = upload_shot(args.orig, args.dream, args.meta, url=args.url, token=args.token)
            photo = result.get("photo") or {}
            print(f"uploaded {photo.get('id', args.meta)}")
            return 0
        if args.cmd == "delete":
            result = delete_shot(args.id, url=args.url, token=args.token)
            print(f"deleted {result.get('deleted', 0)} {args.id}")
            return 0
        watch(
            args.output_dir,
            interval=args.interval,
            once=args.once,
            url=args.url,
            token=args.token,
        )
        return 0
    except KeyboardInterrupt:
        return 0
    except UploadError as err:
        print(str(err), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
