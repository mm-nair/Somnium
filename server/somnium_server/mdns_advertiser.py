"""Advertise the Somnium server as somnium.local.

The camera resolves that name with mDNS when its server host is
``somnium.local``. Binding UDP 5353 can fail when another responder already
owns the port. The HTTP server still runs; set the laptop IP in the camera
portal in that case. ``SOMNIUM_MDNS=0`` skips advertising.
``SOMNIUM_ADVERTISE_IP`` overrides the address put in the A record.
"""

from __future__ import annotations

import logging
import socket
import struct
import threading
import time
from typing import Iterable

logger = logging.getLogger("uvicorn.error")

MDNS_PORT = 5353
MDNS_GROUP = "224.0.0.251"
SERVICE = "_somnium._tcp.local"
INSTANCE = "Somnium._somnium._tcp.local"
HOST = "somnium.local"


def lan_ip() -> str:
    """Best-effort IPv4 of the interface used for outbound traffic."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        address = probe.getsockname()[0]
    except OSError:
        address = "127.0.0.1"
    finally:
        probe.close()
    return address


def encode_name(name: str) -> bytes:
    labels = name.strip(".").split(".") if name.strip(".") else []
    chunks = bytearray()
    for label in labels:
        raw = label.encode("ascii")
        if not 1 <= len(raw) <= 63:
            raise ValueError(f"bad dns label: {label}")
        chunks.append(len(raw))
        chunks.extend(raw)
    chunks.append(0)
    return bytes(chunks)


def _record(name: str, rtype: int, data: bytes, ttl: int = 120) -> bytes:
    return encode_name(name) + struct.pack(">HHIH", rtype, 0x8001, ttl, len(data)) + data


def a_record(ip: str, ttl: int = 120) -> bytes:
    return _record(HOST, 1, socket.inet_aton(ip), ttl)


def ptr_record(ttl: int = 120) -> bytes:
    target = encode_name(INSTANCE)
    return _record(SERVICE, 12, target, ttl)


def srv_record(port: int, ttl: int = 120) -> bytes:
    target = encode_name(HOST)
    data = struct.pack(">HHH", 0, 0, port) + target
    return _record(INSTANCE, 33, data, ttl)


def txt_record(port: int, ttl: int = 120) -> bytes:
    text = f"path=/v1/process;port={port}".encode("ascii")
    if len(text) > 255:
        raise ValueError("txt too long")
    return _record(INSTANCE, 16, bytes([len(text)]) + text, ttl)


def announcement(ip: str, port: int, ident: int = 0) -> bytes:
    answers = a_record(ip) + ptr_record() + srv_record(port) + txt_record(port)
    header = struct.pack(">HHHHHH", ident & 0xFFFF, 0x8400, 0, 4, 0, 0)
    return header + answers


def read_name(packet: bytes, offset: int) -> tuple[str, int]:
    labels: list[str] = []
    jumped = False
    end = offset
    guard = 0
    cursor = offset
    while guard < 20:
        guard += 1
        if cursor >= len(packet):
            raise ValueError("truncated name")
        length = packet[cursor]
        if length == 0:
            cursor += 1
            if not jumped:
                end = cursor
            return ".".join(labels).lower(), end
        if length & 0xC0 == 0xC0:
            if cursor + 1 >= len(packet):
                raise ValueError("bad pointer")
            pointer = ((length & 0x3F) << 8) | packet[cursor + 1]
            if pointer >= len(packet):
                raise ValueError("pointer out of range")
            if not jumped:
                end = cursor + 2
            cursor = pointer
            jumped = True
            continue
        cursor += 1
        if cursor + length > len(packet):
            raise ValueError("truncated label")
        labels.append(packet[cursor : cursor + length].decode("ascii", "replace"))
        cursor += length
    raise ValueError("name loop")


def question_names(packet: bytes) -> list[str]:
    if len(packet) < 12:
        return []
    flags = struct.unpack(">H", packet[2:4])[0]
    if flags & 0x8000:
        return []
    count = struct.unpack(">H", packet[4:6])[0]
    offset = 12
    names: list[str] = []
    for _ in range(count):
        name, offset = read_name(packet, offset)
        if offset + 4 > len(packet):
            break
        names.append(name)
        offset += 4
    return names


def wants_somnium(packet: bytes) -> bool:
    try:
        names = question_names(packet)
    except ValueError:
        return False
    for name in names:
        if name in (HOST, SERVICE, INSTANCE) or name.endswith("._somnium._tcp.local"):
            return True
    return False


def response_for(packet: bytes, ip: str, port: int) -> bytes | None:
    if not wants_somnium(packet):
        return None
    ident = struct.unpack(">H", packet[0:2])[0]
    questions = struct.unpack(">H", packet[4:6])[0]
    offset = 12
    try:
        for _ in range(questions):
            _, offset = read_name(packet, offset)
            if offset + 4 > len(packet):
                raise ValueError("truncated question")
            offset += 4
    except ValueError:
        return announcement(ip, port, ident)
    answers = a_record(ip) + ptr_record() + srv_record(port) + txt_record(port)
    header = struct.pack(">HHHHHH", ident & 0xFFFF, 0x8400, questions, 4, 0, 0)
    return header + packet[12:offset] + answers


def _open_socket(ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.bind(("0.0.0.0", MDNS_PORT))
    group = socket.inet_aton(MDNS_GROUP)
    iface = socket.inet_aton("0.0.0.0" if ip in ("", "0.0.0.0") else ip)
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, group + iface)
    except OSError:
        sock.setsockopt(
            socket.IPPROTO_IP,
            socket.IP_ADD_MEMBERSHIP,
            group + socket.inet_aton("0.0.0.0"),
        )
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, iface)
    except OSError:
        pass
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
    return sock


class Advertiser:
    def __init__(self, ip: str, port: int):
        self.ip = ip
        self.port = port
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._sock: socket.socket | None = None

    def start(self) -> bool:
        if self.ip in ("", "0.0.0.0"):
            logger.warning("mDNS skipped; no address to advertise")
            return False
        try:
            self._sock = _open_socket(self.ip)
        except OSError as err:
            logger.warning(
                "mDNS disabled (%s). Set the laptop IP in the camera portal.",
                err,
            )
            self._sock = None
            return False
        self._thread = threading.Thread(target=self._run, name="somnium-mdns", daemon=True)
        self._thread.start()
        logger.info("mDNS advertising %s -> %s:%s", HOST, self.ip, self.port)
        return True

    def stop(self) -> None:
        self._stop.set()
        sock = self._sock
        self._sock = None
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        thread = self._thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=2)

    def _send(self, packet: bytes, destination: tuple[str, int] | None = None) -> None:
        sock = self._sock
        if sock is None:
            return
        target = destination if destination is not None else (MDNS_GROUP, MDNS_PORT)
        try:
            sock.sendto(packet, target)
        except OSError:
            return

    def _run(self) -> None:
        self._send(announcement(self.ip, self.port))
        next_announce = time.monotonic() + 20
        while not self._stop.is_set():
            sock = self._sock
            if sock is None:
                return
            sock.settimeout(1.0)
            try:
                packet, addr = sock.recvfrom(1500)
            except socket.timeout:
                packet = b""
                addr = None
            except OSError:
                return
            if packet and addr is not None:
                reply = response_for(packet, self.ip, self.port)
                if reply is not None:
                    self._send(reply, (addr[0], addr[1]))
            if time.monotonic() >= next_announce:
                self._send(announcement(self.ip, self.port))
                next_announce = time.monotonic() + 20


def iter_records(packet: bytes) -> Iterable[tuple[str, int]]:
    """Test helper: yield (name, type) for the answer section of one message."""
    if len(packet) < 12:
        return
    answers = struct.unpack(">H", packet[6:8])[0]
    offset = 12
    for _ in range(answers):
        name, offset = read_name(packet, offset)
        if offset + 10 > len(packet):
            return
        rtype, _class, _ttl, rdlength = struct.unpack(">HHIH", packet[offset : offset + 10])
        yield name, rtype
        offset += 10 + rdlength
