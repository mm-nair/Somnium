"""Remote-host config: env overrides, process auth, variation memory, clients."""

import asyncio
import io
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from email.message import EmailMessage
from pathlib import Path
from unittest.mock import Mock, patch
from urllib.request import Request as UrlRequest

from starlette.datastructures import UploadFile
from starlette.requests import Request
from starlette.responses import Response


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))

from somnium_server.auth import ensure_auth_config, tokens_match  # noqa: E402
from somnium_server.config import load_config  # noqa: E402
from somnium_server.pipeline import Pipeline, _VARIATIONS_NAME  # noqa: E402
import somnium_server.app as app_module  # noqa: E402
import usb_bridge  # noqa: E402
import cook  # noqa: E402


TOKEN = "test-process-token"
JPEG = b"\xff\xd8\xff\xd9"


def http_request(headers: list[tuple[bytes, bytes]] | None = None) -> Request:
    return Request(
        {
            "type": "http",
            "method": "POST",
            "path": "/v1/process",
            "headers": headers or [],
            "client": ("127.0.0.1", 1234),
            "scheme": "http",
            "server": ("test", 80),
            "query_string": b"",
        }
    )


class _Body:
    def __init__(self, data: bytes):
        self._data = data
        self.headers = EmailMessage()

    def read(self) -> bytes:
        return self._data

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


class AuthTests(unittest.TestCase):
    def test_tokens_match_rejects_empty_and_overlong(self):
        self.assertTrue(tokens_match("abc", "abc"))
        self.assertFalse(tokens_match("abc", "abd"))
        self.assertFalse(tokens_match("", "abc"))
        self.assertFalse(tokens_match("x" * 257, "x" * 257))

    def test_open_when_token_unset(self):
        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": ""}, clear=False):
            denied = app_module.rejection(http_request())
        self.assertIsNone(denied)

    def test_process_rejects_missing_and_wrong_token(self):
        result = Mock(
            style_id="anime",
            prompt="ok",
            image=JPEG,
            context={"summary": "day"},
            meta={"orig_path": "/out/a.jpg", "dream_path": "/out/b.jpg"},
        )
        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": TOKEN}), patch.object(
            app_module.pipeline, "process", return_value=result
        ) as pipeline:
            missing = asyncio.run(
                app_module.process(
                    request=http_request(),
                    image=UploadFile(io.BytesIO(JPEG)),
                    style="anime",
                    taken_at=None,
                    lat=None,
                    lon=None,
                    force_real=None,
                )
            )
            wrong = asyncio.run(
                app_module.process(
                    request=http_request([(b"x-somnium-token", b"nope")]),
                    image=UploadFile(io.BytesIO(JPEG)),
                    style="anime",
                    taken_at=None,
                    lat=None,
                    lon=None,
                    force_real=None,
                )
            )
        self.assertEqual(missing.status_code, 401)
        self.assertEqual(json.loads(missing.body), {"error": "unauthorized"})
        self.assertEqual(wrong.status_code, 401)
        pipeline.assert_not_called()

    def test_process_accepts_header_or_bearer(self):
        result = Mock(
            style_id="anime",
            prompt="ok",
            image=JPEG,
            context={"summary": "day"},
            meta={"orig_path": "/out/a.jpg", "dream_path": "/out/b.jpg"},
        )
        calls = []

        async def call_next(request):
            calls.append(request)
            return Response(content=b"ok")

        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": TOKEN}), patch.object(
            app_module.pipeline, "process", return_value=result
        ) as pipeline:
            header = asyncio.run(
                app_module.process(
                    request=http_request([(b"x-somnium-token", TOKEN.encode())]),
                    image=UploadFile(io.BytesIO(JPEG)),
                    style="anime",
                    taken_at=None,
                    lat=None,
                    lon=None,
                    force_real=None,
                )
            )
            bearer = asyncio.run(
                app_module.require_process_auth(
                    http_request([(b"authorization", f"Bearer {TOKEN}".encode())]),
                    call_next,
                )
            )
        self.assertEqual(header.status_code, 200)
        self.assertEqual(header.body, JPEG)
        self.assertEqual(bearer.status_code, 200)
        self.assertEqual(calls[0].url.path, "/v1/process")
        pipeline.assert_called_once()

    def test_skip_gallery_is_forwarded_only_when_the_field_says_so(self):
        result = Mock(
            style_id="anime",
            prompt="ok",
            image=JPEG,
            context={"summary": "day"},
            meta={"orig_path": "/out/a.jpg", "dream_path": "/out/b.jpg"},
        )
        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": ""}), patch.object(
            app_module.pipeline, "process", return_value=result
        ) as pipeline:
            asyncio.run(
                app_module.process(
                    request=http_request(),
                    image=UploadFile(io.BytesIO(JPEG)),
                    style="anime",
                    taken_at=None,
                    lat=None,
                    lon=None,
                    force_real=None,
                    skip_gallery="1",
                )
            )
            skipped = pipeline.call_args.kwargs["skip_gallery"]
            asyncio.run(
                app_module.process(
                    request=http_request(),
                    image=UploadFile(io.BytesIO(JPEG)),
                    style="anime",
                    taken_at=None,
                    lat=None,
                    lon=None,
                    force_real=None,
                )
            )
            absent = pipeline.call_args.kwargs["skip_gallery"]
        self.assertTrue(skipped)
        self.assertFalse(absent)

    def test_health_stays_public_and_reports_auth(self):
        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": TOKEN}):
            body = app_module.health()
        self.assertTrue(body["ok"])
        self.assertTrue(body["auth"])
        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": ""}):
            self.assertFalse(app_module.health()["auth"])

    def test_health_answers_while_a_cook_blocks(self):
        result = Mock(
            style_id="anime",
            prompt="ok",
            image=JPEG,
            context={"summary": "day"},
            meta={"orig_path": "/out/a.jpg", "dream_path": "/out/b.jpg"},
        )
        loop_ran = threading.Event()

        def block(**kwargs):
            del kwargs
            if not loop_ran.wait(2):
                raise AssertionError("event loop blocked during cook")
            return result

        async def run():
            task = asyncio.create_task(
                app_module.process(
                    request=http_request(),
                    image=UploadFile(io.BytesIO(JPEG)),
                    style="anime",
                    taken_at=None,
                    lat=None,
                    lon=None,
                    force_real=None,
                )
            )
            await asyncio.sleep(0.05)
            loop_ran.set()
            self.assertTrue(app_module.health()["ok"])
            response = await task
            self.assertEqual(response.status_code, 200)

        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": ""}), patch.object(
            app_module.pipeline, "process", side_effect=block
        ):
            asyncio.run(run())

    def test_require_auth_refuses_to_import_without_a_token(self):
        with patch.dict(
            os.environ,
            {"SOMNIUM_REQUIRE_AUTH": "1", "SOMNIUM_PROCESS_TOKEN": ""},
        ):
            with self.assertRaises(SystemExit) as raised:
                ensure_auth_config()
        self.assertIn("SOMNIUM_PROCESS_TOKEN", str(raised.exception))

        env = os.environ.copy()
        env["SOMNIUM_REQUIRE_AUTH"] = "1"
        env.pop("SOMNIUM_PROCESS_TOKEN", None)
        env["PYTHONPATH"] = str(ROOT / "server")
        proc = subprocess.run(
            [sys.executable, "-c", "import somnium_server.app"],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("SOMNIUM_PROCESS_TOKEN", proc.stderr + proc.stdout)


class ConfigEnvTests(unittest.TestCase):
    def test_env_overrides_host_port_output_and_venue(self):
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "settings.json"
            settings.write_text(
                json.dumps(
                    {
                        "host": "127.0.0.1",
                        "port": 9,
                        "output_dir": "nope",
                        "venue": {"lat": 1, "lon": 2, "label": "file"},
                    }
                )
            )
            with patch.dict(
                os.environ,
                {
                    "SOMNIUM_SETTINGS_PATH": str(settings),
                    "SOMNIUM_HOST": "0.0.0.0",
                    "SOMNIUM_PORT": "9090",
                    "SOMNIUM_OUTPUT_DIR": "/data/outputs",
                    "SOMNIUM_VENUE_LAT": "47.1",
                    "SOMNIUM_VENUE_LON": "-122.2",
                    "SOMNIUM_VENUE_LABEL": "Cloud",
                },
            ):
                cfg = load_config()
        self.assertEqual(cfg["server"]["host"], "0.0.0.0")
        self.assertEqual(cfg["server"]["port"], 9090)
        self.assertEqual(cfg["server"]["output_dir"], "/data/outputs")
        self.assertEqual(cfg["location"]["default_lat"], 47.1)
        self.assertEqual(cfg["location"]["default_lon"], -122.2)
        self.assertEqual(cfg["location"]["default_label"], "Cloud")
        self.assertEqual(cfg["_settings_path"], str(settings))

    def test_port_falls_back_to_port_then_settings(self):
        with patch.dict(os.environ, {"SOMNIUM_PORT": "", "PORT": "7070"}, clear=False):
            cfg = load_config()
        self.assertEqual(cfg["server"]["port"], 7070)

    def test_bad_port_exits(self):
        with patch.dict(os.environ, {"SOMNIUM_PORT": "nope"}):
            with self.assertRaises(SystemExit):
                load_config()


class VariationMemoryTests(unittest.TestCase):
    def test_restart_keeps_the_last_surprise_pick(self):
        cfg = load_config()
        cfg["context"]["provider"] = "stub"
        cfg["prompt"]["provider"] = "template"
        cfg["image"]["provider"] = "echo"
        with tempfile.TemporaryDirectory() as output_dir:
            cfg["server"]["output_dir"] = output_dir
            first = Pipeline(cfg)
            result = first.process(
                image=b"offline image",
                style_id="surprise",
                taken_at="2026-01-01T12:00:00+00:00",
                client_ip=None,
            )
            saved = json.loads((Path(output_dir) / _VARIATIONS_NAME).read_text())
            self.assertEqual(saved["surprise"], result.context["variation"])
            second = Pipeline(cfg)
            again = second.process(
                image=b"offline image",
                style_id="surprise",
                taken_at="2026-01-01T12:00:01+00:00",
                client_ip=None,
            )
            for category, selection in result.context["variation"].items():
                self.assertNotEqual(again.context["variation"][category], selection)


class ClientHeaderTests(unittest.TestCase):
    def test_bridge_sends_token_and_default_url(self):
        with patch.dict(os.environ, {"SOMNIUM_SERVER_URL": "", "SOMNIUM_PROCESS_TOKEN": ""}):
            self.assertEqual(
                usb_bridge.default_process_url(), "http://127.0.0.1:8080/v1/process"
            )
        with patch.dict(
            os.environ, {"SOMNIUM_SERVER_URL": "https://somnium.duckdns.org"}
        ):
            self.assertEqual(
                usb_bridge.default_process_url(),
                "https://somnium.duckdns.org/v1/process",
            )
        with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": TOKEN}), patch.object(
            usb_bridge.urllib.request, "urlopen", return_value=_Body(JPEG)
        ) as opened:
            dream = usb_bridge.post_process(
                JPEG,
                style="anime",
                taken_at="2026-01-01T00:00:00Z",
                lat=None,
                lon=None,
                url="https://example.test/v1/process",
                timeout=5,
            )
        self.assertEqual(dream, JPEG)
        sent: UrlRequest = opened.call_args.args[0]
        self.assertEqual(sent.get_header("X-somnium-token"), TOKEN)
        self.assertEqual(sent.full_url, "https://example.test/v1/process")

    def test_cook_http_sends_token(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "shot.jpg"
            image.write_bytes(JPEG)
            out = Path(tmp) / "out"
            with patch.dict(os.environ, {"SOMNIUM_PROCESS_TOKEN": TOKEN}), patch(
                "urllib.request.urlopen", return_value=_Body(JPEG)
            ) as opened:
                code = cook.cook_http(
                    image,
                    "anime",
                    "2026-01-01T00:00:00Z",
                    None,
                    None,
                    False,
                    "https://somnium.duckdns.org",
                    out,
                )
        self.assertEqual(code, 0)
        sent: UrlRequest = opened.call_args.args[0]
        self.assertEqual(sent.get_header("X-somnium-token"), TOKEN)
        self.assertTrue(sent.full_url.endswith("/v1/process"))


if __name__ == "__main__":
    unittest.main()
