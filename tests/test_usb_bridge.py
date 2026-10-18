import binascii
import io
import json
import sys
import unittest
import urllib.error
from collections import deque
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

import usb_bridge
import usb_cooktest


class SerialStub:
    def __init__(self, chunks=(), on_write=None):
        self.chunks = deque(chunks)
        self.writes = []
        self.on_write = on_write
        self.closed = False

    def read(self, length):
        if not self.chunks:
            return b""
        chunk = self.chunks.popleft()
        if len(chunk) > length:
            self.chunks.appendleft(chunk[length:])
            chunk = chunk[:length]
        return chunk

    def write(self, data):
        self.writes.append(data)
        if self.on_write is not None:
            self.on_write(self, data)
        return len(data)

    def flush(self):
        pass

    def close(self):
        self.closed = True


class MonotonicClock:
    def __init__(self):
        self.value = 0.0

    def __call__(self):
        self.value += 0.001
        return self.value


class StoppingSerialStub(SerialStub):
    def read(self, length):
        if not self.chunks:
            raise KeyboardInterrupt
        return super().read(length)


class UsbBridgeTests(unittest.TestCase):
    transaction_id = "1234abcd"
    jpeg = b"\xff\xd8payload\xff\xd9"

    def setUp(self):
        self.clock = MonotonicClock()
        self.addCleanup(patch.stopall)
        patch.object(usb_bridge.time, "monotonic", self.clock).start()
        patch.object(usb_bridge.time, "sleep").start()
        self.output = io.StringIO()
        self.stdout = redirect_stdout(self.output)
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)

    def cook_header(self, jpeg=None, extra=""):
        jpeg = self.jpeg if jpeg is None else jpeg
        header = (
            f"COOK style=popcomic len={len(jpeg)} id={self.transaction_id} "
            f"crc={binascii.crc32(jpeg):08x}"
        )
        if extra:
            header = f"{header} {extra}"
        return header

    def reply_to_dream(self, ser, data):
        if data.startswith(b"DREAM "):
            ser.chunks.extend([b"READY DR", b"", f"EAM {self.transaction_id}\n".encode()])
        elif data.endswith(b"\nDREAM_END\n"):
            ser.chunks.extend([b"ACK D", b"", f"REAM {self.transaction_id}\n".encode()])

    def process_cook(self, ser, header=None):
        usb_bridge.process_cook(
            ser,
            usb_bridge.SerialLineReader(ser),
            self.cook_header() if header is None else header,
            url="http://unused.invalid",
            timeout=1,
        )

    def test_partial_line_survives_read_deadline(self):
        ser = SerialStub([b"COOK "])
        reader = usb_bridge.SerialLineReader(ser)
        self.assertIsNone(reader.read_line(0.02))
        ser.chunks.extend([b"style=anime\n"])
        self.assertEqual(reader.read_line(0.1), b"COOK style=anime\n")

    def test_complete_exchange_checks_capture_and_waits_for_ack(self):
        ser = SerialStub([self.jpeg, b"\nCOOK_END\n"], self.reply_to_dream)
        backend = patch.object(usb_bridge, "post_process", return_value=self.jpeg).start()
        self.process_cook(ser)
        backend.assert_called_once()
        self.assertEqual(backend.call_args.args[0], self.jpeg)
        self.assertEqual(
            ser.writes,
            [
                f"DREAM {self.transaction_id} {len(self.jpeg)} {binascii.crc32(self.jpeg):08x}\n".encode(),
                self.jpeg + b"\nDREAM_END\n",
            ],
        )
        self.assertIn("accepted by ESP", self.output.getvalue())

    def test_mutated_capture_never_reaches_backend(self):
        corrupted = self.jpeg.replace(b"payload", b"payloae")
        ser = SerialStub([corrupted, b"\nCOOK_END\n"])
        backend = patch.object(usb_bridge, "post_process").start()
        with self.assertRaisesRegex(usb_bridge.ProtocolError, "checksum mismatch"):
            self.process_cook(ser)
        backend.assert_not_called()
        self.assertEqual(ser.writes, [b"ERR 1234abcd COOK checksum mismatch\n"])

    def test_missing_capture_trailer_never_reaches_backend(self):
        ser = SerialStub([self.jpeg, b"\nCOOK_BAD\n"])
        backend = patch.object(usb_bridge, "post_process").start()
        with self.assertRaisesRegex(usb_bridge.ProtocolError, "trailer"):
            self.process_cook(ser)
        backend.assert_not_called()

    def test_short_capture_never_reaches_backend(self):
        ser = SerialStub([self.jpeg[:4]])
        backend = patch.object(usb_bridge, "post_process").start()
        with self.assertRaisesRegex(TimeoutError, "short read"):
            self.process_cook(ser)
        backend.assert_not_called()

    def test_legacy_firmware_gets_explicit_mismatch(self):
        ser = SerialStub()
        with self.assertRaisesRegex(usb_bridge.ProtocolMismatchError, "protocol mismatch"):
            self.process_cook(ser, "COOK style=anime len=123")
        self.assertEqual(ser.writes, [b"ERR bridge protocol mismatch\n"])

    def test_invalid_capture_lengths_and_checksums_are_rejected(self):
        for field in ("len=-1", "len=3", "len=400001", "len=bogus", "crc=zzzzzzzz", "id=123"):
            with self.subTest(field=field):
                name = field.split("=", 1)[0]
                header = " ".join(
                    field if token.startswith(name + "=") else token
                    for token in self.cook_header().split()
                )
                with self.assertRaises(usb_bridge.ProtocolError):
                    usb_bridge.parse_cook_header(header)

    def test_backend_error_reaches_matching_transaction(self):
        ser = SerialStub([self.jpeg, b"\nCOOK_END\n"])
        patch.object(usb_bridge, "post_process", side_effect=urllib.error.URLError("offline")).start()
        with self.assertRaises(urllib.error.URLError):
            self.process_cook(ser)
        self.assertEqual(len(ser.writes), 1)
        self.assertTrue(ser.writes[0].startswith(b"ERR 1234abcd "))

    def test_backend_http_error_body_reaches_terminal_and_device(self):
        for body, expected in (
            (b'{"error":"xAI image edit HTTP 503: overloaded"}', "xAI image edit HTTP 503: overloaded"),
            (b"upstream unavailable", "upstream unavailable"),
            (b"", "Bad Gateway"),
        ):
            with self.subTest(body=body):
                ser = SerialStub([self.jpeg, b"\nCOOK_END\n"])
                error = urllib.error.HTTPError(
                    "http://localhost/v1/process", 502, "Bad Gateway", {}, io.BytesIO(body)
                )
                with patch.object(usb_bridge.urllib.request, "urlopen", side_effect=error):
                    with self.assertRaisesRegex(RuntimeError, expected):
                        self.process_cook(ser)
                self.assertEqual(len(ser.writes), 1)
                self.assertIn(expected.encode(), ser.writes[0])

    def test_no_payload_is_sent_before_receiver_is_ready(self):
        ser = SerialStub()
        with self.assertRaisesRegex(TimeoutError, "no READY"):
            usb_bridge.send_dream(ser, self.jpeg, self.transaction_id, usb_bridge.SerialLineReader(ser), 0.1)
        self.assertEqual(len(ser.writes), 1)
        self.assertTrue(ser.writes[0].startswith(b"DREAM "))

    def test_missing_ack_is_failure_after_payload_write(self):
        ser = SerialStub([b"READY DREAM 1234abcd\n"])
        with self.assertRaisesRegex(TimeoutError, "no ACK"):
            usb_bridge.send_dream(ser, self.jpeg, self.transaction_id, usb_bridge.SerialLineReader(ser), 0.2)
        self.assertEqual(ser.writes[-1], self.jpeg + b"\nDREAM_END\n")
        self.assertNotIn("accepted", self.output.getvalue())

    def test_restarted_board_never_receives_old_payload(self):
        for boot in (b"boot\n", b"READY usb-cook\n", b"ESP-ROM:esp32s3\n"):
            with self.subTest(boot=boot):
                ser = SerialStub([boot])
                with self.assertRaises(usb_bridge.DeviceResetError):
                    usb_bridge.send_dream(ser, self.jpeg, self.transaction_id, usb_bridge.SerialLineReader(ser), 0.2)
                self.assertEqual(len(ser.writes), 1)

    def test_ack_for_another_capture_is_rejected(self):
        ser = SerialStub([b"READY DREAM 1234abcd\nACK DREAM 1234abce\n"])
        with self.assertRaisesRegex(TimeoutError, "no ACK"):
            usb_bridge.send_dream(ser, self.jpeg, self.transaction_id, usb_bridge.SerialLineReader(ser), 0.2)

    def test_stale_transaction_responses_do_not_interrupt_new_capture(self):
        ser = SerialStub([
            b"ERR 11111111 stale cook\n",
            b"ACK DREAM 11111111\n",
            b"READY DREAM 1234abcd\n",
            b"ACK DREAM 1234abcd\n",
        ])
        usb_bridge.send_dream(ser, self.jpeg, self.transaction_id, usb_bridge.SerialLineReader(ser), 0.3)
        self.assertEqual(ser.writes[-1], self.jpeg + b"\nDREAM_END\n")

    def test_new_capture_is_preserved_while_old_result_is_cancelled(self):
        header = self.cook_header().encode() + b"\n"
        ser = SerialStub([header, self.jpeg, b"\nCOOK_END\n"])
        reader = usb_bridge.SerialLineReader(ser)
        with self.assertRaisesRegex(usb_bridge.DeviceResetError, "superseded"):
            usb_bridge.send_dream(ser, self.jpeg, "11111111", reader, 0.2)
        self.assertEqual(len(ser.writes), 1)
        self.assertEqual(reader.read_line(0.1), header)
        meta = usb_bridge.parse_cook_header(header.decode().strip())
        self.assertEqual(usb_bridge.receive_cook(ser, meta), self.jpeg)

    def test_corruption_error_from_esp_is_failure(self):
        ser = SerialStub([b"READY DREAM 1234abcd\nERR 1234abcd dream crc\n"])
        with self.assertRaisesRegex(usb_bridge.ProtocolError, "dream crc"):
            usb_bridge.send_dream(ser, self.jpeg, self.transaction_id, usb_bridge.SerialLineReader(ser), 0.2)

    def test_write_timeout_does_not_replay_frame(self):
        def fail_payload(ser, data):
            if data.startswith(b"DREAM "):
                ser.chunks.append(b"READY DREAM 1234abcd\n")
            else:
                raise usb_bridge.serial.SerialTimeoutException("write timeout")

        ser = SerialStub([self.jpeg, b"\nCOOK_END\n"], fail_payload)
        patch.object(usb_bridge, "post_process", return_value=self.jpeg).start()
        with self.assertRaises(usb_bridge.serial.SerialTimeoutException):
            self.process_cook(ser)
        self.assertEqual(len(ser.writes), 2)
        self.assertNotIn("accepted", self.output.getvalue())

    def test_serial_port_is_exclusive_with_bounded_writes(self):
        serial_factory = patch.object(usb_bridge.serial, "Serial").start()
        usb_bridge.open_serial("/dev/unused")
        self.assertTrue(serial_factory.call_args.kwargs["exclusive"])
        self.assertEqual(serial_factory.call_args.kwargs["write_timeout"], 10)

    def test_reconnect_does_not_replay_interrupted_dream(self):
        def fail_payload(ser, data):
            if data.startswith(b"DREAM "):
                ser.chunks.append(b"READY DREAM 1234abcd\n")
            elif data.endswith(b"\nDREAM_END\n"):
                raise usb_bridge.serial.SerialException("USB disconnected")

        first = SerialStub(
            [self.cook_header().encode() + b"\n", self.jpeg, b"\nCOOK_END\n"],
            fail_payload,
        )
        second = StoppingSerialStub()
        factory = patch.object(usb_bridge, "open_serial", side_effect=[first, second]).start()
        patch.object(usb_bridge, "find_acm_port", return_value="/dev/unused").start()
        patch.object(usb_bridge, "post_process", return_value=self.jpeg).start()
        patch.object(sys, "argv", ["usb_bridge.py", "--port", "/dev/unused"]).start()
        self.assertEqual(usb_bridge.main(), 0)
        self.assertEqual(factory.call_count, 2)
        self.assertEqual(len(second.writes), 1)
        self.assertTrue(second.writes[0].startswith(b"TIME "))
        self.assertTrue(first.closed)
        self.assertTrue(second.closed)
        self.assertNotIn("accepted", self.output.getvalue())

    def test_backend_network_failure_does_not_reconnect_serial(self):
        ser = StoppingSerialStub(
            [self.cook_header().encode() + b"\n", self.jpeg, b"\nCOOK_END\n"]
        )
        factory = patch.object(usb_bridge, "open_serial", return_value=ser).start()
        patch.object(usb_bridge, "post_process", side_effect=urllib.error.URLError("offline")).start()
        patch.object(sys, "argv", ["usb_bridge.py", "--port", "/dev/unused"]).start()
        self.assertEqual(usb_bridge.main(), 0)
        factory.assert_called_once()
        self.assertTrue(ser.writes[-1].startswith(b"ERR 1234abcd "))

    def test_backend_error_fits_firmware_line_buffer(self):
        ser = SerialStub([self.jpeg, b"\nCOOK_END\n"])
        patch.object(usb_bridge, "post_process", side_effect=RuntimeError("é" * 200)).start()
        with self.assertRaises(RuntimeError):
            self.process_cook(ser)
        self.assertLess(len(ser.writes[0]), 96)

    def test_cooktest_requires_esp_completion(self):
        ser = SerialStub([b"HOST DREAM 11 B\n"])
        with self.assertRaisesRegex(TimeoutError, "no OK cooktest"):
            usb_cooktest.wait_cooktest(usb_bridge.SerialLineReader(ser), 0.1)

    def test_cooktest_accepts_explicit_completion(self):
        ser = SerialStub([b"OK cooktest 11\n"])
        usb_cooktest.wait_cooktest(usb_bridge.SerialLineReader(ser), 0.1)

    def test_queue_and_battery_fields_are_posted_with_the_shot(self):
        header = self.cook_header(
            extra=(
                "dev=esp1 seq=9 qid=9 bat=71.5 mv=3812 chg=0 q=2 low=0 "
                "taken_at=2024-01-15T03:00:00Z"
            )
        )
        ser = SerialStub([self.jpeg, b"\nCOOK_END\n"], self.reply_to_dream)
        captured = {}

        class Body(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

        def fake_open(req, timeout=0):
            captured["body"] = req.data
            captured["timeout"] = timeout
            return Body(self.jpeg)

        with patch.object(usb_bridge.urllib.request, "urlopen", fake_open):
            self.process_cook(ser, header)
        body = captured["body"]
        self.assertIn(b'name="device_id"', body)
        self.assertIn(b"esp1", body)
        self.assertIn(b'name="sequence_id"', body)
        self.assertIn(b'name="queue_id"', body)
        self.assertIn(b'name="battery_pct"', body)
        self.assertIn(b"71.5", body)
        self.assertIn(b'name="battery_mv"', body)
        self.assertIn(b"3812", body)
        self.assertIn(b'name="queue_depth"', body)
        self.assertIn(b"2024-01-15T03:00:00Z", body)

    def test_status_line_posts_json_to_the_device_endpoint(self):
        self.assertEqual(
            usb_bridge.status_url_for("http://127.0.0.1:8080/v1/process"),
            "http://127.0.0.1:8080/api/device/status",
        )
        payload = usb_bridge.status_payload(
            usb_bridge.parse_status_line(
                "STATUS dev=esp1 bat=71.5 mv=3812 chg=1 low=0 q=2 seq=9"
            )
        )
        self.assertEqual(payload["device_id"], "esp1")
        self.assertEqual(payload["battery_pct"], "71.5")
        self.assertEqual(payload["queue_depth"], "2")
        captured = {}

        class Body(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

        def fake_open(req, timeout=0):
            captured["url"] = req.full_url
            captured["body"] = json.loads(req.data.decode())
            captured["type"] = req.headers["Content-type"]
            return Body(b"{}")

        with patch.object(usb_bridge.urllib.request, "urlopen", fake_open):
            usb_bridge.post_status(payload, "http://127.0.0.1:8080/api/device/status")
        self.assertEqual(captured["url"], "http://127.0.0.1:8080/api/device/status")
        self.assertEqual(captured["body"]["device_id"], "esp1")
        self.assertEqual(captured["body"]["charging"], "1")
        self.assertIn("application/json", captured["type"])


if __name__ == "__main__":
    unittest.main()
