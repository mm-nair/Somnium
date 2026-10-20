"""Battery telemetry and late-upload idempotency.

Sequence identity is (device_id, sequence_id). A missing device id shares the
"unknown" bucket. A missing sequence id is not idempotent — live cooks that
predate this protocol keep working.

The low-battery cutoff matches firmware Defaults::kLowBatteryPct (15).
"""

from __future__ import annotations

import json
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

LOW_BATTERY_PERCENT = 15.0


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _safe_token(value: str) -> str:
    cleaned = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in value)
    return cleaned[:64] or "unknown"


def _float(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _int(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _boolish(value: Any) -> bool | None:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(int(value))
    text = str(value).strip().lower()
    if text in ("1", "true", "yes", "on", "charging"):
        return True
    if text in ("0", "false", "no", "off"):
        return False
    return None


def _text(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def shot_identity(
    device_id: str | None,
    sequence_id: str | None,
    queue_id: str | None = None,
) -> tuple[str, str | None]:
    """Return (device, sequence) for idempotency. sequence None means 'not keyed'."""
    device = _text(device_id) or "unknown"
    sequence = _text(sequence_id) or _text(queue_id)
    return device, sequence


def normalize_telemetry(
    payload: dict[str, Any],
    *,
    source: str,
    default_device: str = "unknown",
) -> dict[str, Any]:
    """Keep known status fields. Absent fields stay absent so updates can merge."""
    device = _text(payload.get("device_id")) or default_device
    pct = _float(payload.get("battery_pct"))
    volts = _float(payload.get("battery_v"))
    if volts is None:
        mv = _int(payload.get("battery_mv"))
        if mv is not None:
            volts = round(mv / 1000.0, 3)
    charging = _boolish(payload.get("charging"))
    low = _boolish(payload.get("low_battery"))
    if low is None and pct is not None:
        low = pct <= LOW_BATTERY_PERCENT
    depth = _int(payload.get("queue_depth"))
    if depth is not None and depth < 0:
        depth = None
    sequence = _text(payload.get("sequence_id")) or _text(payload.get("queue_id"))
    fields: dict[str, Any] = {
        "device_id": device,
        "source": source,
    }
    if pct is not None:
        fields["battery_pct"] = pct
    if volts is not None:
        fields["battery_v"] = volts
    if charging is not None:
        fields["charging"] = charging
    if low is not None:
        fields["low_battery"] = low
    if depth is not None:
        fields["queue_depth"] = depth
    if sequence is not None:
        fields["last_sequence_id"] = sequence
    return fields


class DeviceStatusStore:
    """Latest battery / queue report per device. JSON file, rewritten on update."""

    def __init__(self, path: Path):
        self.path = path
        self._lock = threading.RLock()
        self._devices: dict[str, dict[str, Any]] = {}
        self._load()

    def _load(self) -> None:
        if not isinstance(self.path, Path) or not self.path.is_file():
            return
        try:
            raw = json.loads(self.path.read_text())
        except (OSError, json.JSONDecodeError, TypeError):
            return
        devices = raw.get("devices") if isinstance(raw, dict) else None
        if not isinstance(devices, list):
            return
        for item in devices:
            if isinstance(item, dict) and item.get("device_id"):
                self._devices[str(item["device_id"])] = dict(item)

    def _save(self) -> None:
        if not isinstance(self.path, Path):
            return
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            payload = {"devices": self.list()}
            self.path.write_text(json.dumps(payload, indent=2))
        except OSError:
            return

    def update(self, fields: dict[str, Any]) -> dict[str, Any]:
        device_id = str(fields.get("device_id") or "unknown")
        with self._lock:
            current = dict(self._devices.get(device_id) or {"device_id": device_id})
            for key, value in fields.items():
                if value is not None:
                    current[key] = value
            current["device_id"] = device_id
            current["updated_at"] = _now()
            self._devices[device_id] = current
            snapshot = dict(current)
            self._save()
            return snapshot

    def list(self, device_id: str | None = None) -> list[dict[str, Any]]:
        with self._lock:
            rows = [dict(item) for item in self._devices.values()]
        rows.sort(key=lambda item: str(item.get("device_id")))
        if device_id:
            rows = [item for item in rows if item.get("device_id") == device_id]
        return rows

    def snapshot(self, device_id: str | None = None) -> dict[str, Any]:
        return {"ok": True, "devices": self.list(device_id)}


class ShotRecord:
    def __init__(self, image: bytes, meta: dict[str, Any]):
        self.image = image
        self.meta = meta


class ShotLedger:
    """Maps device+sequence to the dream JPEG already produced for that shot."""

    def __init__(self, root: Path):
        self.root = root if isinstance(root, Path) else Path(str(root))
        self._lock = threading.Lock()

    def _path(self, device_id: str, sequence_id: str) -> Path:
        name = f"{_safe_token(device_id)}__{_safe_token(sequence_id)}.json"
        return self.root / name

    def lookup(self, device_id: str, sequence_id: str) -> ShotRecord | None:
        path = self._path(device_id, sequence_id)
        with self._lock:
            if not path.is_file():
                return None
            try:
                meta = json.loads(path.read_text())
            except (OSError, json.JSONDecodeError, TypeError):
                return None
            dream_path = meta.get("dream_path") if isinstance(meta, dict) else None
            if not dream_path:
                return None
            try:
                image = Path(dream_path).read_bytes()
            except OSError:
                return None
            if len(image) < 4 or image[:2] != b"\xff\xd8":
                return None
            return ShotRecord(image, meta)

    def remember(
        self,
        device_id: str,
        sequence_id: str,
        *,
        dream_path: Path,
        meta: dict[str, Any],
    ) -> None:
        record = dict(meta)
        record["dream_path"] = str(dream_path)
        record["device_id"] = device_id
        record["sequence_id"] = sequence_id
        path = self._path(device_id, sequence_id)
        with self._lock:
            try:
                self.root.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(record, indent=2))
            except OSError:
                return
