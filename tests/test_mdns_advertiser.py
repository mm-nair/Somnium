import socket
import struct
import unittest

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))

from somnium_server.mdns_advertiser import (
    HOST,
    announcement,
    encode_name,
    question_names,
    response_for,
    wants_somnium,
)


def _query(name: str, ident: int = 0x1234) -> bytes:
    header = struct.pack(">HHHHHH", ident, 0, 1, 0, 0, 0)
    question = encode_name(name) + struct.pack(">HH", 1, 1)
    return header + question


class MdnsAdvertiserTests(unittest.TestCase):
    def test_announcement_carries_the_lan_address(self):
        packet = announcement("192.168.4.20", 8080)
        self.assertEqual(struct.unpack(">H", packet[2:4])[0] & 0x8000, 0x8000)
        self.assertIn(socket.inet_aton("192.168.4.20"), packet)
        self.assertIn(struct.pack(">H", 8080), packet)
        self.assertIn(encode_name(HOST), packet)

    def test_query_for_somnium_local_is_answered(self):
        query = _query("somnium.local")
        self.assertEqual(question_names(query), ["somnium.local"])
        self.assertTrue(wants_somnium(query))
        reply = response_for(query, "10.1.2.3", 8080)
        self.assertIsNotNone(reply)
        self.assertEqual(struct.unpack(">H", reply[0:2])[0], 0x1234)
        self.assertEqual(struct.unpack(">H", reply[2:4])[0] & 0x8000, 0x8000)
        self.assertIn(socket.inet_aton("10.1.2.3"), reply)
        self.assertIn(encode_name("somnium.local"), reply)

    def test_unrelated_query_and_our_own_reply_are_ignored(self):
        self.assertFalse(wants_somnium(_query("printer.local")))
        self.assertIsNone(response_for(_query("printer.local"), "10.0.0.1", 8080))
        own = announcement("10.0.0.8", 8080)
        self.assertFalse(wants_somnium(own))
        self.assertIsNone(response_for(own, "10.0.0.8", 8080))

    def test_service_query_is_answered(self):
        query = _query("_somnium._tcp.local", ident=7)
        reply = response_for(query, "10.0.0.9", 8080)
        self.assertIsNotNone(reply)
        self.assertEqual(struct.unpack(">H", reply[0:2])[0], 7)
        self.assertIn(b"path=/v1/process", reply)


if __name__ == "__main__":
    unittest.main()
