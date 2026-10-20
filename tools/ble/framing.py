"""Somnium BLE payload codec. Mirrors lib/Somnium/BleProto.cpp."""

from __future__ import annotations

SERVICE_UUID = "534f4d4e-1000-4000-8000-534f4d4e494d"
MODES_UUID = "534f4d4e-1001-4000-8000-534f4d4e494d"
MODE_SELECT_UUID = "534f4d4e-1002-4000-8000-534f4d4e494d"
SHUTTER_UUID = "534f4d4e-1003-4000-8000-534f4d4e494d"
STATUS_UUID = "534f4d4e-1004-4000-8000-534f4d4e494d"
DREAM_UUID = "534f4d4e-1005-4000-8000-534f4d4e494d"
PREVIEW_UUID = "534f4d4e-1006-4000-8000-534f4d4e494d"

STATUS_VERSION = 1
STATUS_BYTES = 10
DREAM_BYTES = 20
MODES_UPDATED = 0x01

SCREEN_NAMES = {
    0: "boot",
    1: "mode",
    2: "process",
    3: "review",
    4: "storage",
}

FLAG_BUSY = 1 << 0
FLAG_FAILED = 1 << 1
FLAG_SD = 1 << 2
FLAG_CAMERA = 1 << 3
FLAG_DREAM = 1 << 4
FLAG_BATTERY = 1 << 5
BATTERY_ABSENT = 0xFF

DREAM_IDLE = 0
DREAM_READY = 1
DREAM_FAILED = 2
FORMAT_RGB565 = 1

CHUNK_START = 0x01
CHUNK_END = 0x02
CHUNK_ABORT = 0x80
CHUNK_HEADER = 5
CHUNK_META = 9

PREVIEW_WIDTH = 40
PREVIEW_HEIGHT = 30


def _u16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def _u32(data: bytes, offset: int) -> int:
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def unpack_status(data: bytes) -> dict:
    if len(data) < STATUS_BYTES or data[0] != STATUS_VERSION:
        raise ValueError("status")
    flags = data[3]
    battery = data[8]
    return {
        "screen": data[1],
        "screen_name": SCREEN_NAMES.get(data[1], f"screen-{data[1]}"),
        "mode_index": data[2],
        "flags": flags,
        "busy": bool(flags & FLAG_BUSY),
        "failed": bool(flags & FLAG_FAILED),
        "sd_ready": bool(flags & FLAG_SD),
        "camera_ok": bool(flags & FLAG_CAMERA),
        "dream_ready": bool(flags & FLAG_DREAM),
        "battery_present": bool(flags & FLAG_BATTERY) and battery != BATTERY_ABSENT,
        "sd_count": _u16(data, 4),
        "ram_count": _u16(data, 6),
        "battery_pct": None if battery == BATTERY_ABSENT else battery,
    }


def unpack_dream(data: bytes) -> dict:
    if len(data) < DREAM_BYTES:
        raise ValueError("dream")
    state = data[0]
    return {
        "state": state,
        "state_name": {0: "idle", 1: "ready", 2: "failed"}.get(state, "unknown"),
        "mode_index": data[1],
        "dream_bytes": _u32(data, 2),
        "thumb_width": _u16(data, 6),
        "thumb_height": _u16(data, 8),
        "format": data[10],
        "thumb_bytes": _u32(data, 12),
        "generation": _u16(data, 16),
    }


def unpack_modes(data: bytes) -> dict:
    if len(data) == 1 and data[0] == MODES_UPDATED:
        return {"updated": True, "selected": None, "modes": []}
    if len(data) < 2:
        raise ValueError("modes")
    selected = data[0]
    count = data[1]
    modes = []
    at = 2
    for _ in range(count):
        if at + 2 > len(data):
            raise ValueError("modes truncated")
        index = data[at]
        id_len = data[at + 1]
        at += 2
        if at + id_len + 1 > len(data):
            raise ValueError("modes id")
        style_id = data[at : at + id_len].decode("ascii")
        at += id_len
        label_len = data[at]
        at += 1
        if at + label_len > len(data):
            raise ValueError("modes label")
        label = data[at : at + label_len].decode("ascii")
        at += label_len
        modes.append({"index": index, "id": style_id, "label": label})
    if at != len(data):
        raise ValueError("modes tail")
    return {"updated": False, "selected": selected, "modes": modes}


def parse_chunk(frame: bytes) -> dict:
    if len(frame) < CHUNK_HEADER:
        raise ValueError("chunk")
    flags = frame[0]
    sequence = _u16(frame, 1)
    payload_len = _u16(frame, 3)
    if CHUNK_HEADER + payload_len != len(frame):
        raise ValueError("chunk length")
    payload = frame[CHUNK_HEADER:]
    meta = None
    image = payload
    if flags & CHUNK_START:
        if len(payload) < CHUNK_META:
            raise ValueError("chunk meta")
        meta = {
            "total": _u32(payload, 0),
            "width": _u16(payload, 4),
            "height": _u16(payload, 6),
            "format": payload[8],
        }
        image = payload[CHUNK_META:]
    return {
        "flags": flags,
        "start": bool(flags & CHUNK_START),
        "end": bool(flags & CHUNK_END),
        "abort": bool(flags & CHUNK_ABORT),
        "sequence": sequence,
        "meta": meta,
        "image": image,
    }


class PreviewAssembler:
    """Reassemble preview notifications. START begins a new image."""

    def __init__(self) -> None:
        self.expected = 0
        self.meta = None
        self.buffer = bytearray()
        self.ready = None

    def reset(self) -> None:
        self.expected = 0
        self.meta = None
        self.buffer = bytearray()

    def push(self, frame: bytes) -> bytes | None:
        chunk = parse_chunk(frame)
        self.ready = None
        if chunk["abort"]:
            self.reset()
            return None
        if chunk["sequence"] != self.expected:
            self.reset()
            if not chunk["start"]:
                return None
        if chunk["start"]:
            self.reset()
            self.meta = chunk["meta"]
            self.buffer = bytearray(chunk["image"])
            self.expected = 1
        else:
            self.buffer.extend(chunk["image"])
            self.expected = chunk["sequence"] + 1
        if chunk["end"]:
            total = self.meta["total"] if self.meta else None
            if total is not None and len(self.buffer) == total:
                image = bytes(self.buffer)
                self.ready = image
                self.reset()
                return image
            self.reset()
        return None


def rgb565_to_rgb888(pixel: int) -> tuple[int, int, int]:
    red = (pixel >> 11) & 0x1F
    green = (pixel >> 5) & 0x3F
    blue = pixel & 0x1F
    return ((red * 255) // 31, (green * 255) // 63, (blue * 255) // 31)


def rgb565_ppm(data: bytes, width: int, height: int) -> bytes:
    if len(data) < width * height * 2:
        raise ValueError("preview short")
    rows = bytearray()
    for i in range(width * height):
        pixel = data[i * 2] | (data[i * 2 + 1] << 8)
        rows.extend(rgb565_to_rgb888(pixel))
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    return header + bytes(rows)
