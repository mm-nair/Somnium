#!/usr/bin/env python3
"""USB serial bridge: ESP COOK → localhost /v1/process → DREAM.

Somnium cooks only over USB (no Wi‑Fi on the ESP).

  ./scripts/server.sh          # terminal A
  ./scripts/usb_bridge.sh      # terminal B — leave running while shooting
"""

from __future__ import annotations

import argparse
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from board import add_port_arg, find_port  # noqa: E402

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial required") from exc


def send_time(ser: "serial.Serial") -> None:
    ser.write(f"TIME {int(time.time())}\n".encode())
    ser.flush()


def post_process(
    jpeg: bytes,
    *,
    style: str,
    taken_at: str | None,
    lat: str | None,
    lon: str | None,
    url: str,
    timeout: float,
) -> bytes:
    from datetime import datetime, timezone

    boundary = "----SomniumBridgeBoundary"
    parts: list[bytes] = []

    def field(name: str, value: str) -> None:
        parts.append(
            (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                f"{value}\r\n"
            ).encode()
        )

    field("style", style)
    if not taken_at:
        taken_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    field("taken_at", taken_at)
    if lat:
        field("lat", lat)
    if lon:
        field("lon", lon)
    parts.append(
        (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="image"; filename="capture.jpg"\r\n'
            f"Content-Type: image/jpeg\r\n\r\n"
        ).encode()
    )
    parts.append(jpeg)
    parts.append(f"\r\n--{boundary}--\r\n".encode())
    body = b"".join(parts)

    req = urllib.request.Request(
        url,
        data=body,
        headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Accept": "image/jpeg",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        raise RuntimeError(f"bad jpeg from server ({len(data)} B)")
    return data


def parse_cook_header(line: str) -> dict[str, str]:
    # COOK style=anime len=1234 taken_at=... lat=... lon=...
    out: dict[str, str] = {}
    if not line.startswith("COOK "):
        raise ValueError("not a COOK line")
    for tok in line[5:].split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    if "style" not in out or "len" not in out:
        raise ValueError(f"missing fields: {line!r}")
    return out


def read_exact(ser: serial.Serial, n: int, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    while len(buf) < n and time.monotonic() < deadline:
        chunk = ser.read(n - len(buf))
        if chunk:
            buf.extend(chunk)
        else:
            time.sleep(0.001)
    if len(buf) != n:
        raise TimeoutError(f"short read {len(buf)}/{n}")
    return bytes(buf)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    add_port_arg(ap)
    ap.add_argument("--url", default="http://127.0.0.1:8080/v1/process")
    ap.add_argument("--timeout", type=float, default=240.0)
    args = ap.parse_args()

    port = args.port
    if not port:
        by_id = sorted(Path("/dev/serial/by-id").glob("usb-1a86_*"))
        by_id += sorted(Path("/dev/serial/by-id").glob("usb-Espressif*"))
        if by_id:
            port = str(by_id[0].resolve())
        else:
            port = find_port(None)

    def open_serial() -> serial.Serial:
        s = serial.Serial()
        s.port = port
        s.baudrate = 921600
        s.timeout = 0.2
        s.dtr = False
        s.rts = False
        s.open()
        return s

    ser = open_serial()
    print(f"usb_bridge on {port} → {args.url}", flush=True)
    print("Leave this running while shooting (USB cook).", flush=True)
    print("Tip: don't hold the dial (BOOT) — that drops serial into download mode.", flush=True)
    send_time(ser)
    next_time = time.monotonic() + 30.0

    while True:
        try:
            if time.monotonic() >= next_time:
                send_time(ser)
                next_time = time.monotonic() + 30.0
            line = ser.readline()
            if not line:
                continue
            text = line.decode("utf-8", "replace").strip()
            if not text.startswith("COOK "):
                if text:
                    print(f"| {text}", flush=True)
                    if "READY" in text:
                        send_time(ser)
                        next_time = time.monotonic() + 30.0
                continue
            meta = parse_cook_header(text)
            n = int(meta["len"])
            print(f"COOK style={meta['style']} len={n}", flush=True)
            jpeg = read_exact(ser, n, timeout_s=30)
            end = ser.readline().decode("utf-8", "replace").strip()
            if end == "":
                end = ser.readline().decode("utf-8", "replace").strip()
            if end and end != "COOK_END":
                print(f"warn: expected COOK_END got {end!r}", flush=True)

            dream = post_process(
                jpeg,
                style=meta["style"],
                taken_at=meta.get("taken_at"),
                lat=meta.get("lat"),
                lon=meta.get("lon"),
                url=args.url,
                timeout=args.timeout,
            )
            # Header first + short pause so ESP can leave UI-pump before binary.
            send_time(ser)
            ser.write(f"DREAM {len(dream)}\n".encode())
            ser.flush()
            time.sleep(0.05)
            ser.write(dream)
            ser.write(b"\nDREAM_END\n")
            ser.flush()
            print(f"DREAM {len(dream)} B sent", flush=True)
            next_time = time.monotonic() + 30.0
        except serial.SerialException as exc:
            print(f"serial lost ({exc}) — reconnecting…", flush=True)
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
            while True:
                time.sleep(1.0)
                try:
                    ser = open_serial()
                    send_time(ser)
                    next_time = time.monotonic() + 30.0
                    print(f"reconnected {port}", flush=True)
                    break
                except Exception as reopen_exc:  # noqa: BLE001
                    print(f"waiting for port ({reopen_exc})", flush=True)
        except Exception as exc:  # noqa: BLE001
            msg = str(exc).replace("\n", " ")[:160]
            print(f"ERR {msg}", flush=True)
            try:
                ser.write(f"ERR {msg}\n".encode())
                ser.flush()
            except Exception:  # noqa: BLE001
                pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
