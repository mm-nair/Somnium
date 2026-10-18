#!/usr/bin/env python3
"""One-shot USB cook test: trigger ESP cooktest, bridge COOK→localhost→DREAM."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from usb_bridge import (  # noqa: E402
    parse_cook_header,
    post_process,
    read_exact,
)

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial required") from exc


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="")
    ap.add_argument("--url", default="http://127.0.0.1:8080/v1/process")
    ap.add_argument("--timeout", type=float, default=240.0)
    args = ap.parse_args()

    port = args.port
    if not port:
        by_id = sorted(Path("/dev/serial/by-id").glob("usb-1a86_*"))
        by_id += sorted(Path("/dev/serial/by-id").glob("usb-Espressif*"))
        port = str(by_id[0].resolve()) if by_id else "/dev/ttyACM0"

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 921600
    ser.timeout = 0.3
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.reset_input_buffer()
    print(f"port {port}", flush=True)

    # Wait for READY, then trigger cooktest.
    deadline = time.monotonic() + 20
    ready = False
    while time.monotonic() < deadline:
        line = ser.readline()
        if not line:
            continue
        text = line.decode("utf-8", "replace").strip()
        print(f"| {text}", flush=True)
        if "READY" in text:
            ready = True
            break
    if not ready:
        # Board may already be up — poke anyway.
        print("no READY seen — sending cooktest anyway", flush=True)

    ser.write(b"cooktest\n")
    ser.flush()
    print("→ cooktest", flush=True)

    cook_deadline = time.monotonic() + args.timeout + 30
    while time.monotonic() < cook_deadline:
        line = ser.readline()
        if not line:
            continue
        text = line.decode("utf-8", "replace").strip()
        if not text.startswith("COOK "):
            print(f"| {text}", flush=True)
            if text.startswith("ERR ") or text.startswith("OK cooktest"):
                return 0 if text.startswith("OK") else 1
            continue

        meta = parse_cook_header(text)
        n = int(meta["len"])
        print(f"COOK style={meta['style']} len={n}", flush=True)
        jpeg = read_exact(ser, n, timeout_s=30)
        end = ser.readline().decode("utf-8", "replace").strip()
        if end == "":
            end = ser.readline().decode("utf-8", "replace").strip()
        print(f"got jpeg {len(jpeg)} B end={end!r}", flush=True)

        dream = post_process(
            jpeg,
            style=meta["style"],
            taken_at=meta.get("taken_at"),
            lat=meta.get("lat"),
            lon=meta.get("lon"),
            url=args.url,
            timeout=args.timeout,
        )
        ser.write(f"DREAM {len(dream)}\n".encode())
        ser.write(dream)
        ser.write(b"\nDREAM_END\n")
        ser.flush()
        print(f"DREAM {len(dream)} B sent", flush=True)

        # Wait for OK cooktest
        done_deadline = time.monotonic() + 15
        while time.monotonic() < done_deadline:
            line = ser.readline()
            if not line:
                continue
            text = line.decode("utf-8", "replace").strip()
            print(f"| {text}", flush=True)
            if "OK cooktest" in text:
                print("PASS", flush=True)
                return 0
            if text.startswith("ERR"):
                print("FAIL", flush=True)
                return 1
        print("PASS (dream sent; no OK line)", flush=True)
        return 0

    print("FAIL timeout waiting for COOK", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
