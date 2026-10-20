#!/usr/bin/env python3
"""Wi-Fi bridge: ESP COOK over TCP → localhost /v1/process → DREAM.

The board listens on somnium.local:8090. This process connects, speaks the
same framing as scripts/usb_bridge.py, and reconnects if the link drops.
A cook that was in flight is not replayed.

  ./scripts/server.sh
  ./scripts/wifi_bridge.sh
  ./scripts/wifi_bridge.sh --host 192.168.1.20
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import usb_bridge  # noqa: E402


DEFAULT_HOST = "somnium.local"
DEFAULT_PORT = 8090


class TcpPort:
    """pyserial-shaped socket so the USB cook helpers can run unchanged."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.sock.settimeout(0.2)
        self.closed = False

    def read(self, length: int) -> bytes:
        try:
            data = self.sock.recv(length)
        except socket.timeout:
            return b""
        except OSError as error:
            raise usb_bridge.serial.SerialException(str(error)) from error
        if data == b"":
            raise usb_bridge.serial.SerialException("wifi link closed")
        return data

    def write(self, data: bytes) -> int:
        try:
            self.sock.sendall(data)
        except OSError as error:
            raise usb_bridge.serial.SerialException(str(error)) from error
        return len(data)

    def flush(self) -> None:
        return None

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host",
        default=os.environ.get("SOMNIUM_HOST", DEFAULT_HOST),
        help="Board hostname or IP (default: somnium.local, or SOMNIUM_HOST)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("SOMNIUM_PORT", str(DEFAULT_PORT))),
        help="Board TCP port (default: 8090, or SOMNIUM_PORT)",
    )
    parser.add_argument("--url", default=usb_bridge.default_process_url())
    parser.add_argument("--timeout", type=float, default=usb_bridge.bridge_timeout_seconds())
    return parser.parse_args(argv)


def connect(host: str, port: int, timeout: float = 5.0) -> TcpPort:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return TcpPort(sock)


def serve_once(port: TcpPort, *, url: str, timeout: float) -> None:
    """Read cooks until the board closes the socket."""
    reader = usb_bridge.SerialLineReader(port)
    usb_bridge.send_time(port)
    next_time = time.monotonic() + 30.0
    while True:
        try:
            if time.monotonic() >= next_time:
                usb_bridge.send_time(port)
                next_time = time.monotonic() + 30.0
            line = reader.read_line(0.2)
            if line is None:
                continue
            text = line.decode("utf-8", "replace").strip()
            if not text.startswith("COOK "):
                if text:
                    print(f"| {text}", flush=True)
                continue
            usb_bridge.process_cook(
                port, reader, text, url=url, timeout=timeout
            )
            next_time = time.monotonic() + 30.0
        except usb_bridge.serial.SerialException:
            raise
        except KeyboardInterrupt:
            raise
        except Exception as error:
            print(f"ERR {error}", flush=True)
            next_time = time.monotonic() + 30.0


def main(argv: list[str] | None = None) -> int:
    usb_bridge.load_repo_dotenv()
    args = parse_args(argv)
    print(
        f"wifi_bridge → {args.host}:{args.port}  server {args.url}",
        flush=True,
    )
    print("Leave this running while shooting. Ctrl-C stops it.", flush=True)
    print("USB serial commands (sdlist, sdpull, hello) stay on the cable.", flush=True)
    while True:
        port: TcpPort | None = None
        try:
            port = connect(args.host, args.port)
            print(f"connected {args.host}:{args.port}", flush=True)
            try:
                serve_once(port, url=args.url, timeout=args.timeout)
            finally:
                port.close()
        except KeyboardInterrupt:
            return 0
        except (OSError, usb_bridge.serial.SerialException) as error:
            print(f"wifi link lost ({error}); reconnecting", flush=True)
            time.sleep(1.0)


if __name__ == "__main__":
    raise SystemExit(main())
