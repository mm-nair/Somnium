"""Read per-shot JSON written by the pipeline."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def list_shots(
    output_dir: Path,
    *,
    board: str | None = None,
    device_id: str | None = None,
) -> list[dict[str, Any]]:
    if not output_dir.is_dir():
        return []
    shots: list[dict[str, Any]] = []
    for path in sorted(output_dir.glob("*.json")):
        if path.name.startswith(".") or path.name.endswith(".tmp"):
            continue
        try:
            meta = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(meta, dict) or not isinstance(meta.get("files"), dict):
            continue
        shot_board = meta.get("board") or None
        shot_device = meta.get("device_id") or None
        if board is not None and shot_board != board:
            continue
        if device_id is not None and shot_device != device_id:
            continue
        shots.append(
            {
                "stem": path.stem,
                "style_id": meta.get("style_id"),
                "style_requested": meta.get("style_requested"),
                "device_id": shot_device,
                "board": shot_board,
                "taken_at": meta.get("taken_at"),
                "files": meta.get("files"),
                "prompt": meta.get("prompt"),
            }
        )
    return shots


def boards_with_shots(
    declared: list[dict[str, Any]],
    shots: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Merge registry boards with boards that only appear on saved shots."""
    by_id: dict[str, dict[str, Any]] = {}
    for board in declared:
        board_id = board.get("id")
        if not board_id:
            continue
        by_id[str(board_id)] = {
            "id": str(board_id),
            "name": board.get("name") or board_id,
            "members": list(board.get("members") or []),
        }
    for shot in shots:
        board_id = shot.get("board")
        if not board_id:
            continue
        entry = by_id.setdefault(
            board_id, {"id": board_id, "name": board_id, "members": []}
        )
        device = shot.get("device_id")
        if device and device not in entry["members"]:
            entry["members"].append(device)
    out = []
    for board_id in sorted(by_id):
        entry = by_id[board_id]
        entry["shot_count"] = sum(1 for shot in shots if shot.get("board") == board_id)
        out.append(entry)
    return out
