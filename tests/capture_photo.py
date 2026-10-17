#!/usr/bin/env python3
"""Ask the board to capture a photo (shown on LCD) and save the JPEG here."""

import argparse
import io
import os
import pathlib
import sys
import tempfile
import time
import zlib

import serial
from PIL import Image

from board import add_port_arg, handshake, open_serial, read_line


def save_atomic(path: pathlib.Path, data: bytes):
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix=f".{path.name}.", delete=False
        ) as fh:
            tmp = pathlib.Path(fh.name)
            fh.write(data)
        os.replace(tmp, path)
    finally:
        if tmp is not None:
            tmp.unlink(missing_ok=True)


def capture(ser, width, quality, timeout_s=10.0):
    ser.reset_input_buffer()
    ser.write(f"capture {width} {quality}\n".encode())
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_line(ser, deadline)
        if not line:
            break
        if line.startswith(b"ERR"):
            raise RuntimeError(line.decode(errors="replace").strip())
        if not line.startswith(b"PHOTO "):
            continue
        parts = line.split()
        if len(parts) != 7:
            raise RuntimeError(f"Bad PHOTO header: {line!r}")
        length = int(parts[1])
        crc = int(parts[2], 16)
        cap_ms = int(parts[3])
        enc_ms = int(parts[4])
        w = int(parts[5])
        h = int(parts[6])
        data = bytearray()
        while len(data) < length and time.monotonic() < deadline:
            chunk = ser.read(length - len(data))
            if chunk:
                data.extend(chunk)
        if len(data) != length:
            raise RuntimeError("JPEG truncated")
        trailer = bytearray()
        while len(trailer) < len(b"\nPHOTO_END\n") and time.monotonic() < deadline:
            trailer.extend(ser.read(len(b"\nPHOTO_END\n") - len(trailer)))
        if bytes(trailer) != b"\nPHOTO_END\n":
            raise RuntimeError("Missing PHOTO_END")
        raw = bytes(data)
        if zlib.crc32(raw) != crc:
            raise RuntimeError("CRC mismatch")
        if raw[:2] != b"\xff\xd8" or raw[-2:] != b"\xff\xd9":
            raise RuntimeError("Not a JPEG")
        with Image.open(io.BytesIO(raw)) as img:
            img.load()
            size = img.size
        return raw, size, cap_ms, enc_ms, w, h
    raise RuntimeError("Timed out waiting for PHOTO")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    add_port_arg(parser)
    parser.add_argument("--width", type=int, choices=(320, 640), default=320)
    parser.add_argument("--quality", type=int, default=70)
    parser.add_argument(
        "-o",
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("photo.jpg"),
    )
    args = parser.parse_args()

    try:
        ser = open_serial(args.port)
        try:
            hello = handshake(ser)
            print(hello, flush=True)
            started = time.monotonic()
            raw, size, cap_ms, enc_ms, w, h = capture(
                ser, args.width, args.quality
            )
        finally:
            ser.close()
    except (OSError, serial.SerialException, RuntimeError) as err:
        sys.exit(f"{err}")

    save_atomic(args.output, raw)
    elapsed = time.monotonic() - started
    print(
        f"Saved {args.output} ({len(raw)} B, {size[0]}x{size[1]}) "
        f"in {elapsed:.2f}s — capture {cap_ms}ms encode {enc_ms}ms. Check the LCD."
    )


if __name__ == "__main__":
    main()
