#!/usr/bin/env python3
"""One-shot USB cook test: trigger ESP cooktest, bridge COOK→localhost→DREAM."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from usb_bridge import (
    DeviceResetError,
    ProtocolError,
    SerialLineReader,
    find_acm_port,
    find_port,
    is_board_boot,
    open_serial,
    process_cook,
    write_exact,
)


def wait_cooktest(reader: SerialLineReader, timeout_s: float = 15.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = reader.read_line(deadline - time.monotonic())
        if line is None:
            break
        text = line.decode("utf-8", "replace").strip()
        if text:
            print(f"| {text}", flush=True)
        if text.startswith("OK cooktest "):
            return
        if text.startswith("ERR "):
            raise ProtocolError(text)
        if is_board_boot(text):
            raise DeviceResetError("board restarted during cooktest")
    raise TimeoutError("no OK cooktest from ESP")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="")
    parser.add_argument("--url", default="http://127.0.0.1:8080/v1/process")
    parser.add_argument("--timeout", type=float, default=240.0)
    args = parser.parse_args()
    port = args.port or find_acm_port() or find_port(None)
    ser = open_serial(port)
    reader = SerialLineReader(ser)
    print(f"port {port}", flush=True)

    try:
        deadline = time.monotonic() + 20
        ready = False
        while time.monotonic() < deadline:
            line = reader.read_line(deadline - time.monotonic())
            if line is None:
                break
            text = line.decode("utf-8", "replace").strip()
            print(f"| {text}", flush=True)
            if text.startswith("READY usb-cook"):
                ready = True
                break
        if not ready:
            print("no READY seen; sending cooktest to the running board", flush=True)

        write_exact(ser, b"cooktest\n")
        print("→ cooktest", flush=True)
        deadline = time.monotonic() + args.timeout + 30
        while time.monotonic() < deadline:
            line = reader.read_line(deadline - time.monotonic())
            if line is None:
                break
            text = line.decode("utf-8", "replace").strip()
            if text.startswith("COOK "):
                process_cook(ser, reader, text, url=args.url, timeout=args.timeout)
                wait_cooktest(reader)
                print("PASS", flush=True)
                return 0
            if text:
                print(f"| {text}", flush=True)
            if text.startswith("ERR "):
                raise ProtocolError(text)
            if is_board_boot(text):
                raise DeviceResetError("board restarted during cooktest")
        raise TimeoutError("timeout waiting for COOK")
    except Exception as error:
        print(f"FAIL {error}", flush=True)
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
