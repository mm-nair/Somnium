#!/usr/bin/env python3
"""Pull /somnium/*.jpg from the ESP SD card over USB serial → ThinkPad.

Stop the USB bridge first (it holds the port):

  pkill -f usb_bridge.py
  ./scripts/usb_pull_sd.sh
  ./scripts/usb_bridge.sh
"""

from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from board import add_port_arg, find_port  # noqa: E402

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial required") from exc


def open_serial(port: str) -> "serial.Serial":
    # Match usb_bridge: no DTR/RTS reset (would reboot the board).
    s = serial.Serial()
    s.port = port
    s.baudrate = 921600
    s.timeout = 0.3
    s.dtr = False
    s.rts = False
    s.open()
    return s


def read_line(ser: "serial.Serial", deadline: float) -> bytes:
    while time.monotonic() < deadline:
        line = ser.readline()
        if line:
            return line
    return b""


def read_exact(ser: "serial.Serial", n: int, deadline: float) -> bytes:
    buf = bytearray()
    while len(buf) < n and time.monotonic() < deadline:
        chunk = ser.read(n - len(buf))
        if chunk:
            buf.extend(chunk)
    return bytes(buf)


def wait_ready(ser: "serial.Serial", timeout_s: float = 12.0) -> None:
    """Drain boot chatter; accept board already up via hello handshake."""
    deadline = time.monotonic() + timeout_s
    ser.reset_input_buffer()
    next_hello = 0.0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_hello:
            ser.write(b"hello\n")
            ser.flush()
            next_hello = time.monotonic() + 0.5
        line = ser.readline()
        if not line:
            continue
        text = line.decode("utf-8", "replace").strip()
        if text.startswith("OK somnium") or "READY" in text:
            ser.reset_input_buffer()
            return
    raise RuntimeError("Board not ready (no READY / hello)")


def pull(ser: "serial.Serial", out_dir: Path, timeout_s: float = 120.0) -> list[Path]:
    wait_ready(ser)
    ser.write(b"sdpull\n")
    ser.flush()
    deadline = time.monotonic() + timeout_s
    saved: list[Path] = []
    expect = None

    while time.monotonic() < deadline:
        line = read_line(ser, deadline)
        if not line:
            break
        text = line.decode("utf-8", "replace").strip()
        if text.startswith("ERR "):
            raise RuntimeError(text)
        if text.startswith("SDPULL "):
            expect = int(text.split()[1])
            print(f"SDPULL {expect} files", flush=True)
            continue
        if text.startswith("FILE "):
            parts = text.split()
            if len(parts) != 3:
                raise RuntimeError(f"bad FILE header: {text!r}")
            name, length = parts[1], int(parts[2])
            data = read_exact(ser, length, deadline)
            if len(data) != length:
                raise RuntimeError(f"short read {name}: {len(data)}/{length}")
            end = read_line(ser, deadline).decode("utf-8", "replace").strip()
            if end == "":
                end = read_line(ser, deadline).decode("utf-8", "replace").strip()
            if end != "FILE_END":
                raise RuntimeError(f"missing FILE_END after {name}: {end!r}")
            if data[:2] != b"\xff\xd8":
                raise RuntimeError(f"not jpeg: {name}")
            dest = out_dir / name
            dest.write_bytes(data)
            saved.append(dest)
            print(f"  {name} ({length} B)", flush=True)
            continue
        if text == "SDPULL_END":
            break
        # Ignore boot/log chatter.
        if text:
            print(f"| {text}", flush=True)

    if expect is not None and len(saved) != expect:
        print(f"warn: expected {expect}, got {len(saved)}", flush=True)
    return saved


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    add_port_arg(parser)
    parser.add_argument(
        "-o",
        "--out",
        type=Path,
        default=None,
        help="Output directory (default: demos/sd_YYYYMMDD_HHMMSS)",
    )
    args = parser.parse_args()

    port = find_port(args.port)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out or (ROOT / "demos" / f"sd_{stamp}")
    out_dir.mkdir(parents=True, exist_ok=True)

    try:
        ser = open_serial(port)
    except serial.SerialException as exc:
        raise SystemExit(
            f"{exc}\nStop the bridge first: pkill -f usb_bridge.py"
        ) from exc

    try:
        saved = pull(ser, out_dir)
    finally:
        ser.close()

    print(f"Saved {len(saved)} file(s) → {out_dir}", flush=True)
    if not saved:
        raise SystemExit("No JPEGs on SD (shoot a few plates first).")


if __name__ == "__main__":
    main()
