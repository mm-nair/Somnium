import json
import os
import re
import socket
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
from email.parser import BytesParser
from email.policy import default
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))

from somnium_server.pipeline import Pipeline


ORIG = b"\xff\xd8orig-jpeg\xff\xd9"
DREAM = b"\xff\xd8dream-jpeg\xff\xd9"
TOKEN = "your-token-here"


class _DistinctImage:
    def transform(self, *, image, prompt, image_cfg):
        del image, prompt, image_cfg
        return DREAM


class _UploadHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        self.server.requests.append(
            {
                "path": self.path,
                "token": self.headers.get("X-Somnium-Token"),
                "content_type": self.headers.get("Content-Type", ""),
                "body": body,
            }
        )
        self.server.received.set()
        if self.server.delay:
            time.sleep(self.server.delay)
        status = self.server.status
        payload = b"ok" if status < 400 else b"no"
        self.send_response(status)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        del fmt, args


class _UploadServer(HTTPServer):
    def __init__(self):
        super().__init__(("127.0.0.1", 0), _UploadHandler)
        self.requests = []
        self.received = threading.Event()
        self.delay = 0
        self.status = 204


def _parse_multipart(content_type, body):
    raw = f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n".encode() + body
    message = BytesParser(policy=default).parsebytes(raw)
    parts = {}
    for part in message.iter_parts():
        disposition = part.get("Content-Disposition", "")
        name = part.get_param("name", header="content-disposition")
        filename = part.get_filename()
        parts[name] = {
            "filename": filename,
            "content_type": part.get_content_type(),
            "data": part.get_payload(decode=True),
            "disposition": disposition,
        }
    return parts


def _closed_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


class GalleryUploadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.server = _UploadServer()
        self.addCleanup(self.server.server_close)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.server.shutdown)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/gallery"

    def _pipeline(self):
        cfg = {
            "server": {"output_dir": self.tmp.name},
            "styles": {
                "cartoon": {
                    "label": "Cartoon",
                    "keywords": "ink",
                    "prompt": "ink lines",
                    "real": True,
                }
            },
            "context": {"provider": "stub"},
            "prompt": {"provider": "template"},
            "image": {"provider": "echo"},
            "location": {},
            "creative": {},
        }
        pipe = Pipeline(cfg)
        pipe.stack["image"] = _DistinctImage()
        return pipe

    def _shoot(self, pipe):
        return pipe.process(
            image=ORIG,
            style_id="cartoon",
            taken_at="2026-10-17T19:30:00Z",
            client_ip="127.0.0.1",
        )

    def test_posts_multipart_without_blocking_the_shot(self):
        self.server.delay = 1.2
        timeouts = []
        real_urlopen = urllib.request.urlopen

        def spy(url, data=None, timeout=socket._GLOBAL_DEFAULT_TIMEOUT, **kwargs):
            timeouts.append(timeout)
            return real_urlopen(url, data, timeout, **kwargs)

        started = []
        real_thread = threading.Thread

        class TrackingThread(real_thread):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                started.append(self)

        pipe = self._pipeline()
        with patch.dict(
            os.environ,
            {"SOMNIUM_UPLOAD_URL": self.url, "SOMNIUM_UPLOAD_TOKEN": TOKEN},
        ), patch("somnium_server.pipeline.threading.Thread", TrackingThread), patch(
            "somnium_server.pipeline.urllib.request.urlopen", spy
        ), self.assertLogs("uvicorn.error", level="INFO") as captured:
            started_at = time.perf_counter()
            result = self._shoot(pipe)
            elapsed = time.perf_counter() - started_at
            self.assertLess(elapsed, 0.8)
            self.assertTrue(self.server.received.wait(5))
            self.assertEqual(len(started), 1)
            self.assertTrue(started[0].daemon)
            self.assertTrue(started[0].name.startswith("somnium-gallery-upload-"))
            started[0].join(5)
            self.assertFalse(started[0].is_alive())

        self.assertEqual(timeouts, [20])
        self.assertEqual(len(self.server.requests), 1)
        posted = self.server.requests[0]
        self.assertEqual(posted["path"], "/gallery")
        self.assertEqual(posted["token"], TOKEN)
        self.assertIn("multipart/form-data", posted["content_type"])
        parts = _parse_multipart(posted["content_type"], posted["body"])
        self.assertEqual(set(parts), {"orig", "dream", "meta"})
        stem = result.meta["saved_stem"]
        self.assertEqual(parts["orig"]["data"], ORIG)
        self.assertEqual(parts["orig"]["filename"], f"{stem}_0_orig.jpg")
        self.assertEqual(parts["orig"]["content_type"], "image/jpeg")
        self.assertEqual(parts["dream"]["data"], DREAM)
        self.assertEqual(parts["dream"]["filename"], f"{stem}_1_dream.jpg")
        self.assertEqual(parts["dream"]["content_type"], "image/jpeg")
        self.assertEqual(parts["meta"]["filename"], f"{stem}.json")
        self.assertEqual(parts["meta"]["content_type"], "application/json")
        meta_path = Path(result.meta["orig_path"]).with_name(f"{stem}.json")
        self.assertEqual(parts["meta"]["data"], meta_path.read_bytes())
        meta = json.loads(parts["meta"]["data"])
        self.assertEqual(meta["style_id"], "cartoon")
        self.assertEqual(meta["files"]["orig"], f"{stem}_0_orig.jpg")
        self.assertEqual(meta["files"]["dream"], f"{stem}_1_dream.jpg")
        self.assertEqual(Path(result.meta["orig_path"]).read_bytes(), ORIG)
        self.assertEqual(Path(result.meta["dream_path"]).read_bytes(), DREAM)
        self.assertEqual(result.image, DREAM)
        logged = "\n".join(captured.output)
        self.assertIn(f"Gallery upload succeeded for {stem}", logged)
        self.assertNotIn(TOKEN, logged)
        self.assertNotRegex(logged, re.compile(r"failed", re.IGNORECASE))

    def test_http_failure_is_logged_and_does_not_fail_the_shot(self):
        self.server.status = 500
        pipe = self._pipeline()
        started = []
        real_thread = threading.Thread

        class TrackingThread(real_thread):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                started.append(self)

        with patch.dict(
            os.environ,
            {"SOMNIUM_UPLOAD_URL": self.url, "SOMNIUM_UPLOAD_TOKEN": TOKEN},
        ), patch("somnium_server.pipeline.threading.Thread", TrackingThread), self.assertLogs(
            "uvicorn.error", level="ERROR"
        ) as captured:
            result = self._shoot(pipe)
            self.assertTrue(self.server.received.wait(5))
            started[0].join(5)

        self.assertEqual(result.image, DREAM)
        self.assertTrue(Path(result.meta["orig_path"]).is_file())
        self.assertTrue(Path(result.meta["dream_path"]).is_file())
        logged = "\n".join(captured.output)
        self.assertIn("Gallery upload failed", logged)
        self.assertIn(result.meta["saved_stem"], logged)
        self.assertNotIn(TOKEN, logged)

        refused = f"http://127.0.0.1:{_closed_port()}/gallery"
        started.clear()
        with patch.dict(
            os.environ,
            {"SOMNIUM_UPLOAD_URL": refused, "SOMNIUM_UPLOAD_TOKEN": TOKEN},
        ), patch("somnium_server.pipeline.threading.Thread", TrackingThread), self.assertLogs(
            "uvicorn.error", level="ERROR"
        ) as captured_refused:
            result = self._shoot(pipe)
            started[0].join(5)
        self.assertEqual(result.style_id, "cartoon")
        self.assertIn("Gallery upload failed", "\n".join(captured_refused.output))
        self.assertNotIn(TOKEN, "\n".join(captured_refused.output))

    def test_skips_upload_unless_both_env_vars_are_set(self):
        pipe = self._pipeline()
        cases = (
            {},
            {"SOMNIUM_UPLOAD_URL": "", "SOMNIUM_UPLOAD_TOKEN": ""},
            {"SOMNIUM_UPLOAD_URL": "   ", "SOMNIUM_UPLOAD_TOKEN": "   "},
            {"SOMNIUM_UPLOAD_URL": self.url},
            {"SOMNIUM_UPLOAD_TOKEN": TOKEN},
            {"SOMNIUM_UPLOAD_URL": "", "SOMNIUM_UPLOAD_TOKEN": TOKEN},
            {"SOMNIUM_UPLOAD_URL": self.url, "SOMNIUM_UPLOAD_TOKEN": "  "},
        )
        for overrides in cases:
            with self.subTest(overrides=overrides):
                env = {"SOMNIUM_UPLOAD_URL": "", "SOMNIUM_UPLOAD_TOKEN": ""}
                env.update(overrides)
                with patch.dict(os.environ, env), patch(
                    "somnium_server.pipeline.threading.Thread"
                ) as thread, patch("somnium_server.pipeline.urllib.request.urlopen") as urlopen:
                    result = self._shoot(pipe)
                thread.assert_not_called()
                urlopen.assert_not_called()
                self.assertEqual(Path(result.meta["orig_path"]).read_bytes(), ORIG)
                self.assertEqual(Path(result.meta["dream_path"]).read_bytes(), DREAM)
        self.assertEqual(self.server.requests, [])

    def test_skip_gallery_leaves_the_upload_to_the_camera(self):
        pipe = self._pipeline()
        with patch.dict(
            os.environ,
            {"SOMNIUM_UPLOAD_URL": self.url, "SOMNIUM_UPLOAD_TOKEN": TOKEN},
        ), patch("somnium_server.pipeline.threading.Thread") as thread, patch(
            "somnium_server.pipeline.urllib.request.urlopen"
        ) as urlopen, self.assertLogs("uvicorn.error", level="INFO") as captured:
            result = pipe.process(
                image=ORIG,
                style_id="cartoon",
                taken_at="2026-10-17T19:30:00Z",
                client_ip="127.0.0.1",
                skip_gallery=True,
            )
        thread.assert_not_called()
        urlopen.assert_not_called()
        self.assertEqual(self.server.requests, [])
        self.assertEqual(result.image, DREAM)
        self.assertEqual(Path(result.meta["orig_path"]).read_bytes(), ORIG)
        self.assertEqual(Path(result.meta["dream_path"]).read_bytes(), DREAM)
        self.assertIn(
            f"Gallery upload left to the camera for {result.meta['saved_stem']}",
            "\n".join(captured.output),
        )
        self.assertNotIn(TOKEN, "\n".join(captured.output))


if __name__ == "__main__":
    unittest.main()
