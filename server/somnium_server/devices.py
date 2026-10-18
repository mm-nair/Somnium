"""Device ids and dream boards.

Several cameras share one laptop server. A cook may name a device and a board.
Per-device settings live in ``server/devices.json`` (seeded from
``devices.example.json`` until that file exists). Shots themselves stay in the
output directory; boards are a view over that JSON plus the registry.
"""

from __future__ import annotations

import json
import threading
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from somnium_server.ids import valid_id


class DeviceError(ValueError):
    """Caller-facing rejection (bad id, unknown device on update)."""


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _blank() -> dict[str, Any]:
    return {"devices": {}, "boards": {}}


@dataclass
class ShotIdentity:
    device_id: str | None
    board: str | None
    style_id: str
    style_requested: str
    substituted: bool
    lat: float | None
    lon: float | None
    location_label: str | None


class DeviceDirectory:
    def __init__(self, path: Path, seed: dict[str, Any] | None = None):
        self.path = path
        self.problems: list[str] = []
        self._lock = threading.Lock()
        if path.is_file():
            self._data = self._read(path)
            self._persisted = True
        else:
            self._data = self._normalize(seed or _blank())
            self._persisted = False

    def list_devices(self) -> list[dict[str, Any]]:
        with self._lock:
            return [self._public_device(device_id) for device_id in sorted(self._data["devices"])]

    def get_device(self, device_id: str) -> dict[str, Any] | None:
        self._check_id(device_id, "device_id")
        with self._lock:
            if device_id not in self._data["devices"]:
                return None
            return self._public_device(device_id)

    def register(self, device_id: str, fields: dict[str, Any] | None = None) -> tuple[dict[str, Any], bool]:
        self._check_id(device_id, "device_id")
        fields = fields or {}
        with self._lock:
            created = device_id not in self._data["devices"]
            current = dict(self._data["devices"].get(device_id) or self._new_device(device_id))
            self._apply_fields(current, fields, replace_lists=True)
            current["id"] = device_id
            self._data["devices"][device_id] = current
            self._save_locked()
            return self._public_device(device_id), created

    def update(self, device_id: str, fields: dict[str, Any]) -> dict[str, Any]:
        self._check_id(device_id, "device_id")
        with self._lock:
            if device_id not in self._data["devices"]:
                raise DeviceError(f"unknown device {device_id}")
            current = dict(self._data["devices"][device_id])
            self._apply_fields(current, fields, replace_lists=False)
            self._data["devices"][device_id] = current
            self._save_locked()
            return self._public_device(device_id)

    def ensure(self, device_id: str) -> dict[str, Any]:
        """Return the device, creating a blank record the first time it cooks."""
        self._check_id(device_id, "device_id")
        with self._lock:
            if device_id not in self._data["devices"]:
                self._data["devices"][device_id] = self._new_device(device_id)
                self._save_locked()
            return json.loads(json.dumps(self._data["devices"][device_id]))

    def list_boards(self) -> list[dict[str, Any]]:
        with self._lock:
            return [self._public_board(board_id) for board_id in sorted(self._data["boards"])]

    def get_board(self, board_id: str) -> dict[str, Any] | None:
        self._check_id(board_id, "board")
        with self._lock:
            if board_id not in self._data["boards"]:
                return None
            return self._public_board(board_id)

    def create_board(
        self,
        board_id: str,
        *,
        name: str | None = None,
        members: list[str] | None = None,
    ) -> dict[str, Any]:
        self._check_id(board_id, "board")
        member_ids = self._member_list(members or [])
        with self._lock:
            if board_id in self._data["boards"]:
                raise DeviceError(f"board {board_id} already exists")
            self._data["boards"][board_id] = {
                "name": (name or board_id).strip() or board_id,
                "members": member_ids,
            }
            self._save_locked()
            return self._public_board(board_id)

    def ensure_board(self, board_id: str, *, device_id: str | None = None) -> dict[str, Any]:
        self._check_id(board_id, "board")
        if device_id is not None:
            self._check_id(device_id, "device_id")
        with self._lock:
            board = self._data["boards"].setdefault(
                board_id, {"name": board_id, "members": []}
            )
            members = board.setdefault("members", [])
            if device_id and device_id not in members:
                members.append(device_id)
            self._save_locked()
            return self._public_board(board_id)

    def touch(self, device_id: str, board_id: str | None) -> None:
        with self._lock:
            device = self._data["devices"].get(device_id)
            if device is not None:
                device["last_seen"] = _now()
            if board_id and board_id in self._data["boards"] and device_id:
                members = self._data["boards"][board_id].setdefault("members", [])
                if device_id not in members:
                    members.append(device_id)
            self._save_locked()

    def resolve(
        self,
        *,
        device_id: str | None,
        board: str | None,
        style_id: str,
        styles: dict[str, Any],
        lat: float | None,
        lon: float | None,
    ) -> ShotIdentity:
        if device_id is not None:
            self._check_id(device_id, "device_id")
        if board is not None:
            self._check_id(board, "board")

        requested = style_id
        substituted = False
        label: str | None = None
        use_lat, use_lon = lat, lon

        if device_id:
            device = self.ensure(device_id)
            enabled = list(device.get("enabled_styles") or [])
            default_style = device.get("default_style") or ""
            if enabled and style_id not in enabled:
                if default_style in enabled and default_style in styles:
                    style_id = default_style
                else:
                    style_id = next((item for item in enabled if item in styles), style_id)
                substituted = style_id != requested
            elif style_id not in styles and default_style in styles:
                style_id = default_style
                substituted = True
            loc = device.get("location") or {}
            if _has_coords(loc) and (
                loc.get("override", True) or use_lat is None or use_lon is None
            ):
                use_lat = float(loc["lat"])
                use_lon = float(loc["lon"])
            if loc.get("label"):
                label = str(loc["label"])
            if not board and device.get("default_board"):
                board = str(device["default_board"])
                self._check_id(board, "board")
        if board:
            self.ensure_board(board, device_id=device_id)
        if device_id:
            self.touch(device_id, board)

        return ShotIdentity(
            device_id=device_id,
            board=board,
            style_id=style_id,
            style_requested=requested,
            substituted=substituted,
            lat=use_lat,
            lon=use_lon,
            location_label=label,
        )

    def _public_device(self, device_id: str) -> dict[str, Any]:
        raw = dict(self._data["devices"][device_id])
        raw["id"] = device_id
        return raw

    def _public_board(self, board_id: str) -> dict[str, Any]:
        raw = self._data["boards"][board_id]
        return {
            "id": board_id,
            "name": raw.get("name") or board_id,
            "members": list(raw.get("members") or []),
        }

    def _apply_fields(
        self, current: dict[str, Any], fields: dict[str, Any], *, replace_lists: bool
    ) -> None:
        if "name" in fields and fields["name"] is not None:
            name = str(fields["name"]).strip()
            if not name or len(name) > 80:
                raise DeviceError("name must be 1 to 80 characters")
            current["name"] = name
        if "default_style" in fields:
            style = fields["default_style"] or ""
            if style:
                self._check_style(style)
            current["default_style"] = style
        if "default_board" in fields:
            board = fields["default_board"] or ""
            if board:
                self._check_id(str(board), "board")
            current["default_board"] = board
        if "enabled_styles" in fields and fields["enabled_styles"] is not None:
            styles = fields["enabled_styles"]
            if not isinstance(styles, list):
                raise DeviceError("enabled_styles must be a list")
            current["enabled_styles"] = [self._check_style(item) for item in styles]
        elif replace_lists:
            current.setdefault("enabled_styles", [])
        if "location" in fields and fields["location"] is not None:
            current["location"] = _clean_location(fields["location"])
        current.setdefault("enabled_styles", [])
        current.setdefault("location", {})
        current.setdefault("default_style", "")
        current.setdefault("default_board", "")
        current.setdefault("name", current.get("name") or "")

    def _new_device(self, device_id: str) -> dict[str, Any]:
        return {
            "name": device_id,
            "default_style": "",
            "enabled_styles": [],
            "default_board": "",
            "location": {},
            "created_at": _now(),
        }

    def _check_id(self, value: str, label: str) -> str:
        if not isinstance(value, str) or not valid_id(value):
            raise DeviceError(f"invalid {label}")
        return value

    def _check_style(self, value: Any) -> str:
        if not isinstance(value, str) or not valid_id(value):
            raise DeviceError("invalid style id")
        return value

    def _member_list(self, members: list[Any]) -> list[str]:
        if not isinstance(members, list):
            raise DeviceError("members must be a list")
        cleaned: list[str] = []
        for member in members:
            text = str(member)
            self._check_id(text, "device_id")
            if text not in cleaned:
                cleaned.append(text)
        return cleaned

    def _read(self, path: Path) -> dict[str, Any]:
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as err:
            self.problems.append(f"{path.name}: {err}")
            return _blank()
        if not isinstance(raw, dict):
            self.problems.append(f"{path.name}: expected an object")
            return _blank()
        return self._normalize(raw)

    def _normalize(self, raw: dict[str, Any]) -> dict[str, Any]:
        devices_in = raw.get("devices") if isinstance(raw.get("devices"), dict) else {}
        boards_in = raw.get("boards") if isinstance(raw.get("boards"), dict) else {}
        devices: dict[str, Any] = {}
        for device_id, payload in devices_in.items():
            if not valid_id(str(device_id)) or not isinstance(payload, dict):
                self.problems.append(f"skipped device {device_id}")
                continue
            try:
                record = self._new_device(str(device_id))
                self._apply_fields(record, payload, replace_lists=True)
                if payload.get("name"):
                    record["name"] = str(payload["name"])
                if payload.get("created_at"):
                    record["created_at"] = str(payload["created_at"])
                if payload.get("last_seen"):
                    record["last_seen"] = str(payload["last_seen"])
                devices[str(device_id)] = record
            except DeviceError as err:
                self.problems.append(f"skipped device {device_id}: {err}")
        boards: dict[str, Any] = {}
        for board_id, payload in boards_in.items():
            if not valid_id(str(board_id)) or not isinstance(payload, dict):
                self.problems.append(f"skipped board {board_id}")
                continue
            members: list[str] = []
            for member in payload.get("members") or []:
                if valid_id(str(member)) and str(member) not in members:
                    members.append(str(member))
            boards[str(board_id)] = {
                "name": str(payload.get("name") or board_id),
                "members": members,
            }
        return {"devices": devices, "boards": boards}

    def _save_locked(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(self.path.suffix + ".tmp")
        tmp.write_text(json.dumps(self._data, indent=2) + "\n", encoding="utf-8")
        tmp.replace(self.path)
        self._persisted = True


def _has_coords(location: dict[str, Any]) -> bool:
    return (
        isinstance(location, dict)
        and location.get("lat") is not None
        and location.get("lon") is not None
    )


def _clean_location(raw: Any) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise DeviceError("location must be an object")
    out: dict[str, Any] = {}
    if "lat" in raw or "lon" in raw:
        try:
            lat = float(raw["lat"])
            lon = float(raw["lon"])
        except (KeyError, TypeError, ValueError) as err:
            raise DeviceError("location lat and lon must be numbers") from err
        if not -90 <= lat <= 90 or not -180 <= lon <= 180:
            raise DeviceError("location lat/lon out of range")
        out["lat"] = lat
        out["lon"] = lon
    if "label" in raw and raw["label"]:
        label = str(raw["label"]).strip()
        if len(label) > 80:
            raise DeviceError("location label is too long")
        out["label"] = label
    if "override" in raw:
        if not isinstance(raw["override"], bool):
            raise DeviceError("location.override must be true or false")
        out["override"] = raw["override"]
    elif "lat" in out:
        out["override"] = True
    return out
