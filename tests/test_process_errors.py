import asyncio
import importlib
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

import h11
from starlette.requests import Request
from starlette.datastructures import UploadFile


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))


class ProcessErrorTests(unittest.TestCase):
    def test_generated_metadata_survives_http_serialization(self):
        with patch("somnium_server.config.load_config", return_value={}), patch(
            "somnium_server.pipeline.Pipeline", return_value=Mock()
        ):
            app = importlib.import_module("somnium_server.app")
        for prompt in (
            "a" * 511 + " " + "rest of prompt",
            "  Colored pencil\r\nwith soft shadows\t\x00\x7f  ",
            "Paint café objects with ‘warm’ colors — 水彩 🎨",
        ):
            with self.subTest(prompt=prompt):
                result = Mock(
                    style_id="pencil",
                    prompt=prompt,
                    image=b"\xff\xd8generated image\xff\xd9",
                    context={"summary": "b" * 255 + " " + prompt},
                    meta={"orig_path": "/outputs/original.jpg", "dream_path": "/outputs/dream.jpg"},
                )
                with patch.object(app.pipeline, "process", return_value=result):
                    response = asyncio.run(app.process(
                        request=Request({"type": "http", "client": ("127.0.0.1", 1234)}),
                        image=UploadFile(io.BytesIO(b"\xff\xd8\xff\xd9")),
                        style="pencil", taken_at=None, lat=None, lon=None, force_real=None,
                    ))
                connection = h11.Connection(h11.SERVER)
                connection.receive_data(b"POST /v1/process HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n")
                connection.next_event()
                connection.next_event()
                connection.send(h11.Response(status_code=response.status_code, headers=response.raw_headers))
                body = connection.send(h11.Data(data=response.body))
                connection.send(h11.EndOfMessage())
                self.assertEqual(response.status_code, 200)
                self.assertEqual(body, result.image)
                self.assertLessEqual(len(response.headers["X-Somnium-Prompt"]), 512)
                self.assertLessEqual(len(response.headers["X-Somnium-Context"]), 256)
                self.assertEqual(result.prompt, prompt)

    def test_pipeline_failure_is_logged_and_returned(self):
        with patch("somnium_server.config.load_config", return_value={}), patch(
            "somnium_server.pipeline.Pipeline", return_value=Mock()
        ):
            app = importlib.import_module("somnium_server.app")
        for error in (
            RuntimeError("xAI image edit HTTP 503: overloaded"),
            RuntimeError("prompt HTTP 502: upstream failure"),
            TimeoutError("read timed out"),
            OSError("disk full"),
        ):
            with self.subTest(error=str(error)), patch.object(
                app.pipeline, "process", side_effect=error
            ), self.assertLogs("uvicorn.error", level="ERROR") as captured:
                response = asyncio.run(app.process(
                    request=Request({"type": "http", "client": ("127.0.0.1", 1234)}),
                    image=UploadFile(io.BytesIO(b"\xff\xd8\xff\xd9")),
                    style="anime", taken_at=None, lat=None, lon=None, force_real=None,
                ))
                self.assertEqual(response.status_code, 502)
                self.assertEqual(json.loads(response.body), {"error": str(error)})
            self.assertIn("style=anime", captured.output[0])
            self.assertIn(str(error), captured.output[0])
            self.assertIsNotNone(captured.records[0].exc_info)

    def test_skip_gallery_form_is_forwarded(self):
        with patch("somnium_server.config.load_config", return_value={}), patch(
            "somnium_server.pipeline.Pipeline", return_value=Mock()
        ):
            app = importlib.import_module("somnium_server.app")
        result = Mock(
            style_id="anime",
            prompt="ink",
            image=b"\xff\xd8generated image\xff\xd9",
            context={"summary": "day"},
            meta={"orig_path": "/outputs/original.jpg", "dream_path": "/outputs/dream.jpg"},
        )
        request = Request({"type": "http", "client": ("127.0.0.1", 1234)})
        for raw, expected in (("1", True), ("YES", True), ("true", True), ("on", True), (None, False), ("0", False), ("false", False), ("", False)):
            with self.subTest(raw=raw):
                image = UploadFile(io.BytesIO(b"\xff\xd8\xff\xd9"))
                with patch.object(app.pipeline, "process", return_value=result) as process:
                    response = asyncio.run(app.process(
                        request=request,
                        image=image,
                        style="anime",
                        taken_at=None,
                        lat=None,
                        lon=None,
                        force_real=None,
                        skip_gallery=raw,
                    ))
                self.assertEqual(response.status_code, 200)
                self.assertEqual(process.call_args.kwargs["skip_gallery"], expected)


if __name__ == "__main__":
    unittest.main()
