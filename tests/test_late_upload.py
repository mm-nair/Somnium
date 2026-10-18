"""Late uploads keep the capture clock, and device+sequence is idempotent.

Collected by pytest and by `python -m unittest`. Open-Meteo is mocked.
"""

from __future__ import annotations

import asyncio
import io
import json
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch
from urllib.error import URLError
from urllib.parse import parse_qs, urlparse

from starlette.datastructures import UploadFile
from starlette.requests import Request


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from somnium_server.config import load_config
from somnium_server.device_state import DeviceStatusStore, ShotLedger
from somnium_server.pipeline import Pipeline
from somnium_server.providers.context_openmeteo import OpenMeteoContextProvider
import somnium_server.app as app


JPEG = b"\xff\xd8\xff\xd9"
NOW = datetime(2026, 10, 18, 12, 0, tzinfo=timezone.utc)


class _Body(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


def _history_payload() -> dict:
    # 2024-01-14 19:00 PST (UTC-8) == 2024-01-15 03:00 UTC.
    return {
        "utc_offset_seconds": -28800,
        "hourly": {
            "time": [
                "2024-01-14T18:00",
                "2024-01-14T19:00",
                "2024-01-14T20:00",
            ],
            "temperature_2m": [5.0, 4.2, 3.0],
            "relative_humidity_2m": [80, 82, 84],
            "apparent_temperature": [4.0, 3.1, 2.0],
            "precipitation": [0.0, 0.4, 0.1],
            "weather_code": [0, 61, 3],
            "cloud_cover": [10, 90, 100],
            "wind_speed_10m": [1.0, 2.5, 3.0],
            "surface_pressure": [1010, 1008, 1007],
            "is_day": [0, 0, 0],
        },
        "daily": {
            "time": ["2024-01-14", "2024-01-15"],
            "sunrise": ["2024-01-14T07:50", "2024-01-15T07:49"],
            "sunset": ["2024-01-14T16:40", "2024-01-15T16:41"],
            "uv_index_max": [1.2, 1.1],
            "daylight_duration": [32000, 32100],
        },
    }


def _current_payload() -> dict:
    return {
        "current": {
            "temperature_2m": 21.0,
            "relative_humidity_2m": 40,
            "apparent_temperature": 20.0,
            "is_day": 1,
            "precipitation": 0,
            "weather_code": 1,
            "cloud_cover": 12,
            "wind_speed_10m": 3.0,
            "surface_pressure": 1015,
        },
        "daily": {
            "sunrise": ["2026-06-15T05:10"],
            "sunset": ["2026-06-15T21:05"],
            "uv_index_max": [6],
            "daylight_duration": [57000],
        },
    }


class _CountingImage:
    def __init__(self) -> None:
        self.calls = 0

    def transform(self, *, image, prompt, image_cfg):
        del image, prompt, image_cfg
        self.calls += 1
        return b"\xff\xd8" + f"dream-{self.calls}".encode() + b"\xff\xd9"


class HistoricalContextTests(unittest.TestCase):
    def _enrich(self, taken_at: str, urlopen, now: datetime = NOW):
        with patch(
            "somnium_server.providers.context_openmeteo._now", return_value=now
        ), patch(
            "somnium_server.providers.context_openmeteo.urllib.request.urlopen",
            side_effect=urlopen,
        ):
            return OpenMeteoContextProvider().enrich(
                taken_at=taken_at,
                client_ip=None,
                lat=47.6553,
                lon=-122.3035,
                location_cfg={"default_lat": 0, "default_lon": 0, "default_label": "Seattle"},
                context_cfg={"include_weather": True, "include_astro": True, "include_sun": True},
            )

    def test_old_capture_uses_archive_hour_not_now(self):
        urls: list[str] = []

        def urlopen(req, timeout=8):
            del timeout
            urls.append(req.full_url)
            if "historical-forecast-api" in req.full_url:
                raise URLError("history down")
            if "archive-api" in req.full_url:
                query = parse_qs(urlparse(req.full_url).query)
                self.assertEqual(query["wind_speed_unit"], ["ms"])
                self.assertEqual(query["start_date"], ["2024-01-14"])
                self.assertEqual(query["end_date"], ["2024-01-16"])
                return _Body(json.dumps(_history_payload()).encode())
            raise AssertionError(req.full_url)

        context = self._enrich("2024-01-15T03:00:00Z", urlopen=urlopen)
        self.assertTrue(context["historical"])
        self.assertEqual(context["source"], "open-meteo-archive")
        self.assertEqual(context["time_of_day"], "dusk")
        self.assertEqual(context["day_part"], "night")
        self.assertEqual(context["weather"]["temp_c"], 4.2)
        self.assertEqual(context["weather"]["summary"], "light rain")
        self.assertEqual(context["weather_at"], "2024-01-15T03:00:00+00:00")
        self.assertTrue(context["summary"].startswith("dusk"))
        self.assertFalse(any("://api.open-meteo.com/" in url for url in urls))
        self.assertEqual(context["sun"]["sunrise"], "2024-01-14T07:50")

    def test_recent_capture_keeps_current_weather_and_capture_clock(self):
        urls: list[str] = []

        def urlopen(req, timeout=8):
            del timeout
            urls.append(req.full_url)
            return _Body(json.dumps(_current_payload()).encode())

        now = datetime(2026, 6, 15, 12, 10, tzinfo=timezone.utc)
        context = self._enrich("2026-06-15T11:00:00+00:00", now=now, urlopen=urlopen)
        self.assertFalse(context["historical"])
        self.assertEqual(context["source"], "open-meteo")
        self.assertEqual(context["time_of_day"], "morning")
        self.assertEqual(context["weather"]["temp_c"], 21.0)
        self.assertTrue(urls)
        self.assertTrue(all("://api.open-meteo.com/" in url for url in urls))

    def test_just_inside_and_outside_the_history_window(self):
        hosts: list[str] = []

        def urlopen(req, timeout=8):
            del timeout
            hosts.append(urlparse(req.full_url).netloc)
            if "archive-api" in req.full_url or "historical-forecast-api" in req.full_url:
                return _Body(json.dumps(_history_payload()).encode())
            return _Body(json.dumps(_current_payload()).encode())

        now = datetime(2026, 6, 15, 12, 0, tzinfo=timezone.utc)
        recent = self._enrich("2026-06-15T10:31:00+00:00", now=now, urlopen=urlopen)
        late = self._enrich("2026-06-15T10:29:00+00:00", now=now, urlopen=urlopen)
        self.assertFalse(recent["historical"])
        self.assertEqual(hosts[0], "api.open-meteo.com")
        self.assertTrue(late["historical"])
        self.assertNotEqual(hosts[-1], "api.open-meteo.com")

    def test_history_failure_still_uses_the_capture_clock(self):
        def urlopen(req, timeout=8):
            del req, timeout
            raise URLError("offline")

        context = self._enrich("2024-01-15T03:00:00+00:00", urlopen=urlopen)
        self.assertTrue(context["historical"])
        self.assertEqual(context["source"], "stub")
        self.assertEqual(context["time_of_day"], "night")
        self.assertEqual(context["time_basis"], "capture")
        self.assertTrue(context["taken_at"].startswith("2024-01-15T03:00:00"))

    def test_pipeline_sidecar_keeps_original_timestamp_and_archive_weather(self):
        def urlopen(req, timeout=8):
            del timeout
            if "historical-forecast-api" in req.full_url:
                raise URLError("history down")
            if "archive-api" in req.full_url:
                return _Body(json.dumps(_history_payload()).encode())
            raise AssertionError(req.full_url)

        with tempfile.TemporaryDirectory() as tmp, patch(
            "somnium_server.providers.context_openmeteo._now", return_value=NOW
        ), patch(
            "somnium_server.providers.context_openmeteo.urllib.request.urlopen",
            side_effect=urlopen,
        ):
            cfg = load_config()
            cfg["server"]["output_dir"] = tmp
            cfg["context"]["provider"] = "openmeteo"
            cfg["prompt"]["provider"] = "template"
            cfg["image"]["provider"] = "echo"
            result = Pipeline(cfg).process(
                image=JPEG,
                style_id="anime",
                taken_at="2024-01-15T03:00:00Z",
                client_ip=None,
                lat=47.6553,
                lon=-122.3035,
                battery_pct=71.5,
                battery_mv=3812,
                charging=0,
                queue_depth=2,
                device_id="esp1",
                sequence_id="9",
                queue_id="9",
            )
            meta = json.loads(
                (Path(tmp) / f"{result.meta['saved_stem']}.json").read_text()
            )
        self.assertEqual(meta["taken_at"], "2024-01-15T03:00:00Z")
        self.assertTrue(meta["context"]["historical"])
        self.assertEqual(meta["context"]["time_of_day"], "dusk")
        self.assertEqual(meta["context"]["weather"]["temp_c"], 4.2)
        self.assertEqual(meta["capture"]["sequence_id"], "9")
        self.assertEqual(meta["battery"]["millivolts"], 3812)
        self.assertEqual(meta["battery"]["percent"], 71.5)
        self.assertFalse(meta["battery"]["charging"])


class IdempotentUploadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        cfg = load_config()
        cfg["server"]["output_dir"] = str(root)
        cfg["context"]["provider"] = "stub"
        cfg["prompt"]["provider"] = "template"
        cfg["image"]["provider"] = "echo"
        self.pipeline = Pipeline(cfg)
        self.images = _CountingImage()
        self.pipeline.stack["image"] = self.images
        self.saved = (app.pipeline, app.ledger, app.status_store)
        app.pipeline = self.pipeline
        app.ledger = ShotLedger(root / "ledger")
        app.status_store = DeviceStatusStore(root / "device_status.json")
        self.addCleanup(self._restore)

    def _restore(self):
        app.pipeline, app.ledger, app.status_store = self.saved

    def _post(self, **kwargs):
        defaults = dict(
            style="anime",
            taken_at="2024-01-15T03:00:00Z",
            lat=47.6553,
            lon=-122.3035,
            force_real=None,
            device_id="esp1",
            sequence_id="9",
            queue_id="9",
            battery_pct=71.5,
            battery_mv=3812,
            charging=0,
            queue_depth=2,
            low_battery=0,
        )
        defaults.update(kwargs)
        return asyncio.run(
            app.process(
                request=Request({"type": "http", "client": ("127.0.0.1", 9)}),
                image=UploadFile(io.BytesIO(JPEG), filename="capture.jpg"),
                **defaults,
            )
        )

    def test_same_device_and_sequence_does_not_run_twice(self):
        first = self._post()
        second = self._post(battery_pct=40.0, queue_depth=0, style="pencil")
        self.assertEqual(first.status_code, 200)
        self.assertEqual(second.body, first.body)
        self.assertNotIn("x-somnium-idempotent", {k.lower() for k in first.headers})
        self.assertEqual(second.headers["x-somnium-idempotent"], "replay")
        self.assertEqual(second.headers["x-somnium-sequence"], "9")
        self.assertEqual(self.images.calls, 1)
        self.assertEqual(len(list(Path(self.tmp.name).glob("*_0_orig.jpg"))), 1)
        status = app.device_status(device_id="esp1")
        self.assertEqual(len(status["devices"]), 1)
        device = status["devices"][0]
        self.assertEqual(device["battery_pct"], 40.0)
        self.assertEqual(device["queue_depth"], 0)
        self.assertAlmostEqual(device["battery_v"], 3.812)
        self.assertFalse(device["charging"])
        self.assertFalse(device["low_battery"])
        sidecar = next(Path(self.tmp.name).glob("*.json"))
        if sidecar.name == "device_status.json":
            sidecars = [
                path
                for path in Path(self.tmp.name).glob("*.json")
                if path.name != "device_status.json"
            ]
            sidecar = sidecars[0]
        meta = json.loads(sidecar.read_text())
        self.assertEqual(meta["taken_at"], "2024-01-15T03:00:00Z")

    def test_other_device_or_sequence_is_a_new_shot(self):
        self._post()
        self._post(device_id="esp2")
        self._post(sequence_id="10", queue_id="10")
        self.assertEqual(self.images.calls, 3)

    def test_missing_sequence_is_not_idempotent(self):
        self._post(sequence_id=None, queue_id=None, device_id=None)
        self._post(sequence_id=None, queue_id=None, device_id=None)
        self.assertEqual(self.images.calls, 2)
        self.assertEqual(len(list(Path(self.tmp.name).glob("*_0_orig.jpg"))), 2)

    def test_queue_id_alone_dedupes(self):
        self._post(sequence_id=None, queue_id="44", device_id=None)
        replay = self._post(sequence_id=None, queue_id="44", device_id=None)
        self.assertEqual(self.images.calls, 1)
        self.assertEqual(replay.headers["x-somnium-idempotent"], "replay")
        self.assertEqual(app.device_status()["devices"][0]["device_id"], "unknown")

    def test_deleted_dream_is_processed_again(self):
        self._post()
        dream = next(Path(self.tmp.name).glob("*_1_dream.jpg"))
        dream.unlink()
        self._post()
        self.assertEqual(self.images.calls, 2)

    def test_status_endpoint_merges_periodic_reports(self):
        self._post(battery_pct=10, low_battery=None)
        listed = app.device_status()
        self.assertTrue(listed["devices"][0]["low_battery"])

        async def receive():
            body = json.dumps(
                {
                    "device_id": "esp1",
                    "battery_pct": 90,
                    "battery_v": 4.1,
                    "charging": True,
                    "queue_depth": 5,
                    "low_battery": 1,
                    "sequence_id": 12,
                }
            ).encode()
            return {"type": "http.request", "body": body, "more_body": False}

        request = Request(
            {
                "type": "http",
                "method": "POST",
                "headers": [(b"content-type", b"application/json")],
            },
            receive,
        )
        posted = asyncio.run(app.device_status_post(request))
        self.assertTrue(posted["ok"])
        self.assertTrue(posted["device"]["low_battery"])
        self.assertEqual(posted["device"]["queue_depth"], 5)
        self.assertEqual(posted["device"]["last_sequence_id"], "12")
        self.assertEqual(posted["device"]["source"], "status")
        self.assertEqual(app.device_status(device_id="missing")["devices"], [])

        reloaded = DeviceStatusStore(Path(self.tmp.name) / "device_status.json")
        again = reloaded.list("esp1")[0]
        self.assertEqual(again["battery_pct"], 90)
        self.assertTrue(again["charging"])

    def test_shots_without_telemetry_leave_status_empty(self):
        asyncio.run(
            app.process(
                request=Request({"type": "http", "client": ("127.0.0.1", 9)}),
                image=UploadFile(io.BytesIO(JPEG), filename="capture.jpg"),
                style="anime",
                taken_at=None,
                lat=None,
                lon=None,
                force_real=None,
            )
        )
        self.assertEqual(app.device_status()["devices"], [])


if __name__ == "__main__":
    unittest.main()
