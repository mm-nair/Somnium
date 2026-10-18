"""Camera-free check that POST /v1/process accepts the Wi-Fi multipart cook."""

from __future__ import annotations

import json
import os
import socket
import threading
import unittest
import urllib.request
from email.parser import BytesParser
from email.policy import default
from pathlib import Path
from unittest.mock import patch

import uvicorn


ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("SOMNIUM_MDNS", "0")

import sys

sys.path.insert(0, str(ROOT / "server"))

from somnium_server import app as app_module  # noqa: E402
from somnium_server.pipeline import ProcessResult  # noqa: E402


JPEG = b"\xff\xd8wifi-jpeg\xff\xd9"
DREAM = b"\xff\xd8wifi-dream\xff\xd9"


def _multipart(fields, files):
    boundary = "SomniumBoundary7f3a"
    chunks = []
    for name, value in fields:
        chunks.append(
            (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                f"{value}\r\n"
            ).encode()
        )
    for name, filename, content_type, data in files:
        chunks.append(
            (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
                f"Content-Type: {content_type}\r\n\r\n"
            ).encode()
        )
        chunks.append(data)
        chunks.append(b"\r\n")
    chunks.append(f"--{boundary}--\r\n".encode())
    body = b"".join(chunks)
    return body, f"multipart/form-data; boundary={boundary}"


class _Server(uvicorn.Server):
    def install_signal_handlers(self):
        return


def _free_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


class WifiUploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ["SOMNIUM_MDNS"] = "0"
        cls.port = _free_port()
        config = uvicorn.Config(
            app_module.app,
            host="127.0.0.1",
            port=cls.port,
            log_level="error",
        )
        cls.server = _Server(config)
        cls.thread = threading.Thread(target=cls.server.run, daemon=True)
        cls.thread.start()
        for _ in range(100):
            if cls.server.started:
                break
            threading.Event().wait(0.05)
        if not cls.server.started:
            raise RuntimeError("wifi upload test server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.server.should_exit = True
        cls.thread.join(timeout=5)

    def test_health_names_the_mdns_host_and_upload_path(self):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/health", timeout=5) as response:
            payload = json.loads(response.read().decode())
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["mdns"], "somnium.local")
        self.assertEqual(payload["upload"], "/v1/process")

    def test_camera_multipart_returns_the_dream_jpeg(self):
        captured = {}

        def fake_process(**kwargs):
            captured.update(kwargs)
            return ProcessResult(
                image=DREAM,
                original=kwargs["image"],
                prompt="wifi prompt",
                context={"summary": "lan"},
                style_id=kwargs["style_id"],
                meta={"orig_path": "shot_0_orig.jpg", "dream_path": "shot_1_dream.jpg"},
            )

        body, content_type = _multipart(
            [
                ("style", "anime"),
                ("taken_at", "2026-10-18T15:04:00Z"),
                ("lat", "47.65530"),
                ("lon", "-122.30350"),
                ("source", "wifi"),
                ("skip_gallery", "1"),
            ],
            [("image", "capture.jpg", "image/jpeg", JPEG)],
        )
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/v1/process",
            data=body,
            headers={"Content-Type": content_type, "Accept": "image/jpeg"},
            method="POST",
        )
        with patch.object(app_module.pipeline, "process", side_effect=fake_process):
            with urllib.request.urlopen(request, timeout=5) as response:
                dream = response.read()
                header = response.headers["X-Somnium-Style"]
        self.assertEqual(dream, DREAM)
        self.assertEqual(header, "anime")
        self.assertEqual(captured["image"], JPEG)
        self.assertEqual(captured["style_id"], "anime")
        self.assertEqual(captured["source"], "wifi")
        self.assertTrue(captured["skip_gallery"])
        self.assertAlmostEqual(captured["lat"], 47.6553)
        self.assertAlmostEqual(captured["lon"], -122.3035)
        parsed = _parse(content_type, body)
        self.assertEqual(parsed["image"]["data"], JPEG)
        self.assertEqual(parsed["source"]["data"], b"wifi")

    def test_non_jpeg_is_rejected(self):
        body, content_type = _multipart(
            [("style", "anime"), ("source", "wifi")],
            [("image", "capture.jpg", "image/jpeg", b"not-a-jpeg")],
        )
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/v1/process",
            data=body,
            headers={"Content-Type": content_type},
            method="POST",
        )
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request, timeout=5)
        self.assertEqual(raised.exception.code, 400)

    def test_examples_do_not_commit_a_gallery_token(self):
        gitignore = (ROOT / ".gitignore").read_text()
        self.assertIn("include/wifi_credentials.h", gitignore)
        self.assertIn("include/server_config.h", gitignore)
        server = (ROOT / "include/server_config.example.h").read_text()
        wifi = (ROOT / "include/wifi_credentials.example.h").read_text()
        self.assertIn('#define SOMNIUM_GALLERY_TOKEN ""', server)
        self.assertIn('#define SOMNIUM_GALLERY_URL ""', server)
        self.assertIn('#define SOMNIUM_WIFI_SSID ""', wifi)
        self.assertIn('#define SOMNIUM_WIFI_PASSWORD ""', wifi)
        self.assertNotRegex(server, r'SOMNIUM_GALLERY_TOKEN "[^"]+"')
        self.assertNotRegex(wifi, r'SOMNIUM_WIFI_PASSWORD "[^"]+"')


def _parse(content_type, body):
    message = BytesParser(policy=default).parsebytes(
        f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n".encode() + body
    )
    parts = {}
    for part in message.iter_parts():
        name = part.get_param("name", header="content-disposition")
        payload = part.get_payload(decode=True)
        parts[name] = {"data": payload if payload is not None else b""}
    return parts


if __name__ == "__main__":
    unittest.main()
