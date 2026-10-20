#!/usr/bin/env python3
"""USB serial bridge: ESP COOK → POST /v1/process → DREAM.

Somnium can also cook over Wi-Fi. See scripts/wifi_bridge.py.
This process is the USB serial path. Point the POST at a remote server with
SOMNIUM_SERVER_URL or --url. SOMNIUM_PROCESS_TOKEN is sent as X-Somnium-Token
when it is set. Unset, the local server stays open and no header is sent.

  ./scripts/server.sh          # terminal A — leave running (or the Oracle host)
  ./scripts/usb_bridge.sh      # terminal B — ONLY while shooting

Stop the bridge before flash / sd pull / other serial tools:
  pkill -f usb_bridge.py
"""

from __future__ import annotations

import argparse
import binascii
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))


def load_repo_dotenv() -> None:
    """Load repo .env without overriding variables already set in the shell."""
    sys.path.insert(0, str(ROOT / "server"))
    from somnium_server.config import load_dotenv

    load_dotenv(ROOT / ".env")


def default_process_url() -> str:
    raw = os.environ.get("SOMNIUM_SERVER_URL", "").strip().rstrip("/")
    if not raw:
        return "http://127.0.0.1:8080/v1/process"
    if raw.endswith("/v1/process"):
        return raw
    return raw + "/v1/process"


def process_token() -> str:
    return os.environ.get("SOMNIUM_PROCESS_TOKEN", "").strip()


# The board waits kCookTimeoutMs (300s), then fails the shot. Stay under that
# so this process can still write ERR. Caddy's 360s is the proxy budget on
# the VM, not this client timeout.
BOARD_COOK_TIMEOUT_S = 300.0


def bridge_timeout_seconds(raw: str | None = None) -> float:
    if raw is None:
        raw = os.environ.get("SOMNIUM_PROCESS_TIMEOUT", "")
    text = raw.strip()
    if not text:
        return 240.0
    try:
        value = float(text)
    except ValueError:
        return 240.0
    if value <= 0:
        return 240.0
    if value >= BOARD_COOK_TIMEOUT_S:
        return BOARD_COOK_TIMEOUT_S - 10.0
    return value


from board import add_port_arg, find_port  # noqa: E402

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial required") from exc


class ProtocolError(RuntimeError):
    pass


class ProtocolMismatchError(ProtocolError):
    pass


class DeviceResetError(ProtocolError):
    pass


class SerialLineReader:
    def __init__(self, ser: serial.Serial):
        self.ser = ser
        self.pending = bytearray()

    def read_line(self, timeout_s: float) -> bytes | None:
        newline = self.pending.find(b"\n")
        if newline >= 0:
            line = bytes(self.pending[: newline + 1])
            del self.pending[: newline + 1]
            return line
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = read_serial(self.ser, 1)
            if not chunk:
                continue
            self.pending.extend(chunk)
            if chunk == b"\n":
                line = bytes(self.pending)
                self.pending.clear()
                return line
            if len(self.pending) > 512:
                self.pending.clear()
                raise ProtocolError("serial line too long")
        return None

    def put_back(self, line: bytes) -> None:
        self.pending[:0] = line


def read_serial(ser: serial.Serial, length: int) -> bytes:
    try:
        return ser.read(length)
    except serial.SerialException:
        raise
    except OSError as error:
        raise serial.SerialException(str(error)) from error


def write_exact(ser: serial.Serial, data: bytes) -> None:
    try:
        written = ser.write(data)
        if written != len(data):
            raise serial.SerialException(f"short write {written}/{len(data)}")
        ser.flush()
    except serial.SerialException:
        raise
    except OSError as error:
        raise serial.SerialException(str(error)) from error


def send_time(ser: serial.Serial) -> None:
    write_exact(ser, f"TIME {int(time.time())}\n".encode())


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

    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Accept": "image/jpeg",
    }
    token = process_token()
    if token:
        headers["X-Somnium-Token"] = token
    req = urllib.request.Request(
        url,
        data=body,
        headers=headers,
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
    except urllib.error.HTTPError as error:
        with error:
            detail = error.read(4096).decode("utf-8", "replace").strip()
        try:
            payload = json.loads(detail)
            if isinstance(payload, dict):
                detail = str(payload.get("error") or detail)
        except json.JSONDecodeError:
            pass
        detail = detail or str(error.reason)
        raise RuntimeError(f"backend HTTP {error.code}: {detail}") from error
    if (
        not 4 <= len(data) <= 400000
        or data[:2] != b"\xff\xd8"
        or data[-2:] != b"\xff\xd9"
    ):
        raise RuntimeError(f"bad jpeg from server ({len(data)} B)")
    return data


def parse_cook_header(line: str) -> dict[str, str]:
    out: dict[str, str] = {}
    if not line.startswith("COOK "):
        raise ValueError("not a COOK line")
    for tok in line[5:].split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    if "id" not in out or "crc" not in out:
        raise ProtocolMismatchError("bridge protocol mismatch; flash current firmware")
    if "style" not in out or "len" not in out:
        raise ProtocolError(f"missing fields: {line!r}")
    for name in ("id", "crc"):
        if len(out[name]) != 8 or any(
            c not in "0123456789abcdefABCDEF" for c in out[name]
        ):
            raise ProtocolError(f"invalid COOK {name}")
        out[name] = out[name].lower()
    try:
        length = int(out["len"])
    except ValueError as error:
        raise ProtocolError("invalid COOK length") from error
    if not 4 <= length <= 400000:
        raise ProtocolError(f"invalid COOK length {length}")
    return out


def read_exact(ser: serial.Serial, n: int, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    while len(buf) < n and time.monotonic() < deadline:
        chunk = read_serial(ser, n - len(buf))
        if chunk:
            buf.extend(chunk)
        else:
            time.sleep(0.001)
    if len(buf) != n:
        raise TimeoutError(f"short read {len(buf)}/{n}")
    return bytes(buf)


def receive_cook(ser: serial.Serial, meta: dict[str, str]) -> bytes:
    jpeg = read_exact(ser, int(meta["len"]), timeout_s=30)
    trailer = read_exact(ser, len(b"\nCOOK_END\n"), timeout_s=5)
    if trailer != b"\nCOOK_END\n":
        raise ProtocolError("invalid COOK trailer")
    if binascii.crc32(jpeg) != int(meta["crc"], 16):
        raise ProtocolError("COOK checksum mismatch")
    if jpeg[:2] != b"\xff\xd8" or jpeg[-2:] != b"\xff\xd9":
        raise ProtocolError("invalid COOK JPEG")
    return jpeg


def is_board_boot(text: str) -> bool:
    return text == "boot" or text.startswith(("READY usb-cook", "rst:", "ESP-ROM:"))


def wait_dream_response(
    reader: SerialLineReader,
    expected: str,
    transaction_id: str,
    timeout_s: float,
) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = reader.read_line(deadline - time.monotonic())
        if line is None:
            break
        text = line.decode("utf-8", "replace").strip()
        if text == f"{expected} DREAM {transaction_id}":
            return
        if is_board_boot(text):
            raise DeviceResetError("board restarted during cook; take another photo")
        if text.startswith("COOK "):
            reader.put_back(line)
            raise DeviceResetError("new photo superseded previous cook")
        if text.startswith("ERR "):
            fields = text.split(maxsplit=2)
            response_id = fields[1] if len(fields) > 1 else ""
            if (
                len(response_id) == 8
                and all(c in "0123456789abcdefABCDEF" for c in response_id)
                and response_id.lower() != transaction_id
            ):
                continue
            raise ProtocolError(text)
        if text.startswith(("READY DREAM ", "ACK DREAM ")):
            response_id = text.split()[-1]
            if (
                len(response_id) == 8
                and all(c in "0123456789abcdefABCDEF" for c in response_id)
                and response_id.lower() != transaction_id
            ):
                continue
            raise ProtocolError(f"unexpected transfer response: {text}")
        if text:
            print(f"| {text}", flush=True)
    raise TimeoutError(f"no {expected} DREAM {transaction_id} from ESP")


def send_dream(
    ser: serial.Serial,
    dream: bytes,
    transaction_id: str,
    reader: SerialLineReader,
    timeout_s: float = 15.0,
) -> None:
    if (
        not 4 <= len(dream) <= 400000
        or dream[:2] != b"\xff\xd8"
        or dream[-2:] != b"\xff\xd9"
    ):
        raise ProtocolError("invalid DREAM JPEG")
    checksum = binascii.crc32(dream)
    write_exact(ser, f"DREAM {transaction_id} {len(dream)} {checksum:08x}\n".encode())
    wait_dream_response(reader, "READY", transaction_id, timeout_s)
    write_exact(ser, dream + b"\nDREAM_END\n")
    wait_dream_response(reader, "ACK", transaction_id, timeout_s)


def process_cook(
    ser: serial.Serial,
    reader: SerialLineReader,
    line: str,
    *,
    url: str,
    timeout: float,
) -> None:
    transaction_id = None
    try:
        meta = parse_cook_header(line)
        transaction_id = meta["id"]
        print(
            f"COOK style={meta['style']} len={meta['len']} id={transaction_id}",
            flush=True,
        )
        jpeg = receive_cook(ser, meta)
        dream = post_process(
            jpeg,
            style=meta["style"],
            taken_at=meta.get("taken_at"),
            lat=meta.get("lat"),
            lon=meta.get("lon"),
            url=url,
            timeout=timeout,
        )
        send_dream(ser, dream, transaction_id, reader)
        print(f"DREAM {len(dream)} B accepted by ESP id={transaction_id}", flush=True)
    except ProtocolMismatchError:
        write_exact(ser, b"ERR bridge protocol mismatch\n")
        raise
    except (DeviceResetError, serial.SerialException):
        raise
    except Exception as error:
        message = str(error).replace("\r", " ").replace("\n", " ")
        message = message.encode("ascii", "replace")[:80].decode("ascii")
        if transaction_id is not None:
            write_exact(ser, f"ERR {transaction_id} {message}\n".encode())
        else:
            write_exact(ser, f"ERR bridge {message}\n".encode())
        raise


def open_serial(port: str) -> serial.Serial:
    ser = serial.Serial(
        port=None, baudrate=921600, timeout=0.2, write_timeout=10, exclusive=True
    )
    ser.port = port
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.reset_input_buffer()
    return ser


def find_acm_port(preferred: str | None = None) -> str | None:
    if preferred and Path(preferred).exists():
        return preferred
    by_id = sorted(Path("/dev/serial/by-id").glob("usb-1a86_*"))
    by_id += sorted(Path("/dev/serial/by-id").glob("usb-Espressif*"))
    if by_id:
        return str(by_id[0].resolve())
    ports = sorted(Path("/dev").glob("ttyACM*"))
    return str(ports[0]) if ports else None


def main() -> int:
    load_repo_dotenv()
    ap = argparse.ArgumentParser(description=__doc__)
    add_port_arg(ap)
    ap.add_argument("--url", default=default_process_url())
    ap.add_argument("--timeout", type=float, default=bridge_timeout_seconds())
    args = ap.parse_args()

    port = args.port or find_acm_port() or find_port(None)

    def reconnect(deadline_s: float = 60.0) -> serial.Serial:
        nonlocal port
        try:
            ser.close()
        except Exception:
            pass
        end = time.monotonic() + deadline_s
        last_log = 0.0
        while time.monotonic() < end:
            path = find_acm_port(port)
            if path:
                try:
                    s = open_serial(path)
                    port = path
                    print(f"reconnected {port}", flush=True)
                    return s
                except Exception as reopen_exc:
                    if time.monotonic() - last_log > 3.0:
                        print(f"waiting for port ({reopen_exc})", flush=True)
                        last_log = time.monotonic()
            else:
                if time.monotonic() - last_log > 3.0:
                    print("waiting for USB (unplug/replug Somnium if stuck)…", flush=True)
                    last_log = time.monotonic()
            time.sleep(0.5)
        raise TimeoutError("USB serial did not return")

    ser = open_serial(port)
    reader = SerialLineReader(ser)
    print(f"usb_bridge on {port} → {args.url}", flush=True)
    print("Leave this running while shooting (USB cook).", flush=True)
    print("Stop before flash/sd-pull:  pkill -f usb_bridge.py", flush=True)
    print("Tip: don't hold the dial (GPIO0/BOOT) during cook — USB drops.", flush=True)
    send_time(ser)
    next_time = time.monotonic() + 30.0
    try:
        while True:
            try:
                if time.monotonic() >= next_time:
                    send_time(ser)
                    next_time = time.monotonic() + 30.0
                line = reader.read_line(0.2)
                if line is None:
                    continue
                text = line.decode("utf-8", "replace").strip()
                if not text.startswith("COOK "):
                    if text:
                        print(f"| {text}", flush=True)
                        if text.startswith("READY usb-cook"):
                            send_time(ser)
                            next_time = time.monotonic() + 30.0
                    continue
                process_cook(ser, reader, text, url=args.url, timeout=args.timeout)
                next_time = time.monotonic() + 30.0
            except serial.SerialException as error:
                print(f"serial lost ({error}); reconnecting for the next photo", flush=True)
                ser = reconnect(90.0)
                reader = SerialLineReader(ser)
                send_time(ser)
                next_time = time.monotonic() + 30.0
            except Exception as error:
                print(f"ERR {error}", flush=True)
                next_time = time.monotonic() + 30.0
    except KeyboardInterrupt:
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
