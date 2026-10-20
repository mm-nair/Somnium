import binascii
import io
import os
import socket
import sys
import threading
import unittest
from collections import deque
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

import usb_bridge
import wifi_bridge


class ScriptedPort:
    def __init__(self, chunks, fail_on_dream=False):
        self.chunks = deque(chunks)
        self.writes = []
        self.closed = False
        self.fail_on_dream = fail_on_dream

    def read(self, length):
        if not self.chunks:
            if self.fail_on_dream:
                raise usb_bridge.serial.SerialException("wifi link closed")
            raise KeyboardInterrupt
        chunk = self.chunks.popleft()
        if len(chunk) > length:
            self.chunks.appendleft(chunk[length:])
            chunk = chunk[:length]
        return chunk

    def write(self, data):
        self.writes.append(data)
        if self.fail_on_dream and data.startswith(b"DREAM "):
            self.chunks.append(b"READY DREAM 1234abcd\n")
        if self.fail_on_dream and data.endswith(b"\nDREAM_END\n"):
            raise usb_bridge.serial.SerialException("dropped during dream")
        return len(data)

    def flush(self):
        return None

    def close(self):
        self.closed = True


class WifiBridgeTests(unittest.TestCase):
    jpeg = b"\xff\xd8payload\xff\xd9"

    def setUp(self):
        self.output = io.StringIO()
        self.stdout = redirect_stdout(self.output)
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)

    def cook_bytes(self):
        header = (
            f"COOK style=anime len={len(self.jpeg)} id=1234abcd "
            f"crc={binascii.crc32(self.jpeg):08x}\n"
        )
        return header.encode() + self.jpeg + b"\nCOOK_END\n"

    def test_defaults_follow_mdns_name_and_env(self):
        args = wifi_bridge.parse_args([])
        self.assertEqual(args.host, "somnium.local")
        self.assertEqual(args.port, 8090)
        with patch.dict(
            "os.environ",
            {"SOMNIUM_BOARD_HOST": "10.1.2.3", "SOMNIUM_BOARD_PORT": "9000"},
        ):
            args = wifi_bridge.parse_args([])
        self.assertEqual(args.host, "10.1.2.3")
        self.assertEqual(args.port, 9000)
        # Server bind address. Must not steer the board socket.
        with patch.dict(
            "os.environ", {"SOMNIUM_HOST": "0.0.0.0", "SOMNIUM_PORT": "8080"}
        ):
            args = wifi_bridge.parse_args([])
        self.assertEqual(args.host, "somnium.local")
        self.assertEqual(args.port, 8090)
        args = wifi_bridge.parse_args(["--host", "192.168.1.20", "--port", "8091"])
        self.assertEqual((args.host, args.port), ("192.168.1.20", 8091))
        with patch.dict(os.environ, {"SOMNIUM_SERVER_URL": ""}, clear=False):
            os.environ.pop("SOMNIUM_SERVER_URL", None)
            args = wifi_bridge.parse_args([])
        self.assertEqual(args.url, "http://127.0.0.1:8080/v1/process")
        with patch.dict(os.environ, {"SOMNIUM_SERVER_URL": "https://somnium.example"}):
            args = wifi_bridge.parse_args([])
        self.assertEqual(args.url, "https://somnium.example/v1/process")

    def test_tcp_cook_uses_the_usb_framing(self):
        done = threading.Event()
        errors = []

        def recv_until(conn: socket.socket, token: bytes, limit: int = 200000) -> bytes:
            buf = b""
            while token not in buf:
                chunk = conn.recv(4096)
                if not chunk:
                    raise RuntimeError(f"closed before {token!r} in {buf!r}")
                buf += chunk
                if len(buf) > limit:
                    raise RuntimeError("peer sent too much")
            return buf

        def esp(conn: socket.socket) -> None:
            try:
                conn.settimeout(3)
                # Bridge sends TIME, then waits. The board opens the cook.
                conn.sendall(self.cook_bytes())
                incoming = recv_until(conn, b"\n")
                # Drop a clock sync if it was still buffered ahead of DREAM.
                dream_at = incoming.find(b"DREAM ")
                if dream_at < 0:
                    incoming = incoming[incoming.find(b"\n") + 1 :] + recv_until(conn, b"\n")
                    dream_at = incoming.find(b"DREAM ")
                header = incoming[dream_at:].split(b"\n", 1)[0].decode()
                parts = header.split()
                if len(parts) != 4 or parts[0] != "DREAM":
                    raise RuntimeError(f"bad dream header {header!r}")
                transaction_id = parts[1]
                length = int(parts[2])
                checksum = int(parts[3], 16)
                conn.sendall(f"READY DREAM {transaction_id}\n".encode())
                payload = b""
                while len(payload) < length + len(b"\nDREAM_END\n"):
                    chunk = conn.recv(4096)
                    if not chunk:
                        raise RuntimeError("closed during dream payload")
                    payload += chunk
                body = payload[:length]
                trailer = payload[length : length + len(b"\nDREAM_END\n")]
                if trailer != b"\nDREAM_END\n":
                    raise RuntimeError(f"bad dream trailer {trailer!r}")
                if body != self.jpeg or binascii.crc32(body) != checksum:
                    raise RuntimeError("dream bytes mismatch")
                conn.sendall(f"ACK DREAM {transaction_id}\n".encode())
            except Exception as error:  # noqa: BLE001 — reported through errors
                errors.append(error)
            finally:
                try:
                    conn.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                conn.close()
                done.set()

        server = socket.socket()
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        port_number = server.getsockname()[1]

        def accept_one() -> None:
            conn, _addr = server.accept()
            esp(conn)

        thread = threading.Thread(target=accept_one)
        thread.start()
        self.addCleanup(server.close)
        backend = patch.object(
            usb_bridge, "post_process", return_value=self.jpeg
        ).start()
        self.addCleanup(patch.stopall)
        link = wifi_bridge.connect("127.0.0.1", port_number, timeout=2)
        try:
            with self.assertRaises(usb_bridge.serial.SerialException):
                wifi_bridge.serve_once(
                    link, url="http://127.0.0.1:8080/v1/process", timeout=2
                )
        finally:
            link.close()
        thread.join(3)
        self.assertTrue(done.is_set())
        self.assertEqual(errors, [])
        backend.assert_called_once()
        self.assertIn("accepted by ESP", self.output.getvalue())

    def test_reconnect_does_not_replay_a_dropped_dream(self):
        first = ScriptedPort([self.cook_bytes()], fail_on_dream=True)
        second = ScriptedPort([])
        scripted = [first, second]

        def scripted_connect(_host, _port, timeout=5.0):
            del timeout
            if not scripted:
                raise KeyboardInterrupt
            return scripted.pop(0)

        patch.object(wifi_bridge, "connect", side_effect=scripted_connect).start()
        patch.object(usb_bridge, "post_process", return_value=self.jpeg).start()
        patch.object(wifi_bridge.time, "sleep").start()
        self.addCleanup(patch.stopall)
        self.assertEqual(wifi_bridge.main(["--host", "somnium.local"]), 0)
        dream_writes = [
            data
            for data in first.writes
            if data.startswith(b"DREAM ") or data.endswith(b"\nDREAM_END\n")
        ]
        self.assertEqual(dream_writes[0].startswith(b"DREAM 1234abcd "), True)
        self.assertTrue(dream_writes[-1].endswith(b"\nDREAM_END\n"))
        self.assertEqual(len(dream_writes), 2)
        self.assertEqual(len(second.writes), 1)
        self.assertTrue(second.writes[0].startswith(b"TIME "))
        self.assertTrue(first.closed)
        self.assertTrue(second.closed)
        self.assertNotIn("accepted", self.output.getvalue())


if __name__ == "__main__":
    unittest.main()
