import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from fastapi.testclient import TestClient

import somnium_server.app as server_app
from somnium_server.devices import DeviceDirectory, DeviceError
from somnium_server.eval.harness import scene_jpeg
from somnium_server.pipeline import Pipeline
from somnium_server.styles.registry import StyleRegistry


JPEG = scene_jpeg()


def _cfg(tmp: Path) -> dict:
    creative = json.loads((ROOT / "creative/creative.json").read_text())
    registry = StyleRegistry(creative=creative)
    return {
        "server": {"output_dir": str(tmp / "outputs")},
        "styles": registry.as_map(),
        "_style_registry": registry,
        "_devices": DeviceDirectory(tmp / "devices.json"),
        "context": {"provider": "stub"},
        "prompt": {"provider": "template", "temperature": 0.45, "system": ""},
        "image": {"provider": "echo"},
        "location": {
            "default_lat": 47.6553,
            "default_lon": -122.3035,
            "default_label": "Seattle",
        },
        "creative": creative,
    }


class DeviceBoardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.cfg = _cfg(Path(self.tmp.name))
        self.pipeline = Pipeline(self.cfg)
        self.devices = self.cfg["_devices"]

    def _cook(self, **kwargs):
        payload = {
            "image": JPEG,
            "style_id": "anime",
            "taken_at": "2026-10-17T21:00:00Z",
            "client_ip": "127.0.0.1",
            "lat": 1.0,
            "lon": 2.0,
        }
        payload.update(kwargs)
        return self.pipeline.process(**payload)

    def _meta(self, result):
        return json.loads(Path(result.meta["orig_path"]).with_suffix("").with_name(
            result.meta["saved_stem"] + ".json"
        ).read_text())

    def test_anonymous_shot_keeps_request_coordinates(self):
        result = self._cook()
        meta = self._meta(result)
        self.assertIsNone(meta["device_id"])
        self.assertIsNone(meta["board"])
        self.assertEqual(meta["style_requested"], "anime")
        self.assertEqual(result.context["lat"], 1.0)
        self.assertEqual(result.context["location_label"], "Seattle")
        self.assertFalse((Path(self.tmp.name) / "devices.json").exists())

    def test_two_cameras_share_a_board_and_override_location(self):
        self.devices.register(
            "cam-kitchen",
            {
                "name": "Kitchen",
                "default_style": "watercolor",
                "enabled_styles": ["watercolor", "pencil"],
                "default_board": "table",
                "location": {
                    "lat": 47.65,
                    "lon": -122.30,
                    "label": "Kitchen",
                    "override": True,
                },
            },
        )
        self.devices.register(
            "cam-bar",
            {
                "name": "Bar",
                "default_style": "popcomic",
                "enabled_styles": ["popcomic", "anime"],
                "location": {
                    "lat": 40.0,
                    "lon": -73.0,
                    "label": "Bar",
                    "override": True,
                },
            },
        )
        kitchen = self._cook(device_id="cam-kitchen", style_id="anime")
        bar = self._cook(device_id="cam-bar", style_id="anime", board="table")
        kitchen_meta = self._meta(kitchen)
        bar_meta = self._meta(bar)
        self.assertEqual(kitchen.style_id, "watercolor")
        self.assertEqual(kitchen_meta["style_requested"], "anime")
        self.assertEqual(kitchen_meta["device_id"], "cam-kitchen")
        self.assertEqual(kitchen_meta["board"], "table")
        self.assertEqual(kitchen.context["lat"], 47.65)
        self.assertEqual(kitchen.context["location_label"], "Kitchen")
        self.assertEqual(bar.style_id, "anime")
        self.assertEqual(bar_meta["device_id"], "cam-bar")
        self.assertEqual(bar_meta["board"], "table")
        self.assertEqual(bar.context["location_label"], "Bar")
        board = self.devices.get_board("table")
        self.assertEqual(board["members"], ["cam-kitchen", "cam-bar"])

    def test_override_false_keeps_the_request_coordinates(self):
        self.devices.register(
            "cam-roam",
            {
                "location": {
                    "lat": 10.0,
                    "lon": 20.0,
                    "label": "Roam",
                    "override": False,
                }
            },
        )
        result = self._cook(device_id="cam-roam", lat=3.5, lon=4.5)
        self.assertEqual(result.context["lat"], 3.5)
        self.assertEqual(result.context["lon"], 4.5)
        self.assertEqual(result.context["location_label"], "Roam")

    def test_unknown_device_is_registered_on_first_cook(self):
        result = self._cook(device_id="cam-new", board="porch")
        self.assertEqual(result.meta["device_id"], "cam-new")
        record = self.devices.get_device("cam-new")
        self.assertEqual(record["enabled_styles"], [])
        self.assertIn("last_seen", record)
        self.assertIn("cam-new", self.devices.get_board("porch")["members"])

    def test_invalid_ids_are_rejected(self):
        with self.assertRaises(DeviceError):
            self._cook(device_id="bad id")
        with self.assertRaises(DeviceError):
            self.devices.create_board("table")
            self.devices.create_board("table")

    def test_http_lists_boards_and_shots(self):
        self.devices.register(
            "cam-kitchen",
            {"name": "Kitchen", "default_board": "table"},
        )
        self.devices.create_board("table", name="Table", members=["cam-kitchen"])
        self._cook(device_id="cam-kitchen", style_id="pencil", board="table")
        self._cook(device_id="cam-bar", style_id="anime", board="table")
        with patch.object(server_app, "pipeline", self.pipeline), patch.object(
            server_app, "cfg", self.cfg
        ):
            client = TestClient(server_app.app)
            health = client.get("/health")
            styles = client.get("/v1/styles")
            devices = client.get("/v1/devices")
            missing = client.get("/v1/devices/nope")
            boards = client.get("/v1/boards")
            shots = client.get("/v1/boards/table/shots")
            created = client.post(
                "/v1/devices",
                json={
                    "id": "cam-porch",
                    "name": "Porch",
                    "default_style": "anime",
                    "enabled_styles": ["anime"],
                },
            )
            patched = client.patch(
                "/v1/devices/cam-porch",
                json={"default_board": "table"},
            )
            conflict = client.post("/v1/boards", json={"id": "table", "name": "Table"})
            cooked = client.post(
                "/v1/process",
                files={"image": ("shot.jpg", JPEG, "image/jpeg")},
                data={"style": "anime", "device_id": "cam-porch", "board": "table"},
            )
        self.assertEqual(health.status_code, 200)
        self.assertIn("pencil", health.json()["styles"])
        self.assertIn("popcomic", [item["id"] for item in styles.json()["styles"]])
        self.assertEqual(missing.status_code, 404)
        ids = {item["id"] for item in devices.json()["devices"]}
        self.assertIn("cam-kitchen", ids)
        self.assertIn("cam-bar", ids)
        table = next(item for item in boards.json()["boards"] if item["id"] == "table")
        self.assertGreaterEqual(table["shot_count"], 2)
        self.assertEqual({shot["device_id"] for shot in shots.json()["shots"]}, {"cam-kitchen", "cam-bar"})
        self.assertEqual(created.status_code, 201)
        self.assertEqual(patched.json()["default_board"], "table")
        self.assertEqual(conflict.status_code, 409)
        self.assertEqual(cooked.status_code, 200)
        self.assertEqual(cooked.headers["x-somnium-device"], "cam-porch")
        self.assertEqual(cooked.headers["x-somnium-board"], "table")
        self.assertTrue(cooked.content.startswith(b"\xff\xd8"))

    def test_process_rejects_a_bad_device_before_cooking(self):
        with patch.object(server_app, "pipeline", self.pipeline), patch.object(
            server_app, "cfg", self.cfg
        ):
            client = TestClient(server_app.app)
            response = client.post(
                "/v1/process",
                files={"image": ("shot.jpg", JPEG, "image/jpeg")},
                data={"style": "anime", "device_id": "bad id"},
            )
        self.assertEqual(response.status_code, 400)
        self.assertIn("device_id", response.json()["error"])

    def test_reload_picks_up_a_user_style(self):
        folder = Path(self.tmp.name) / "styles"
        folder.mkdir()
        self.cfg["_style_registry"].styles_dir = folder
        with patch.object(server_app, "pipeline", self.pipeline), patch.object(
            server_app, "cfg", self.cfg
        ):
            client = TestClient(server_app.app)
            (folder / "vapor.json").write_text(
                json.dumps(
                    {
                        "id": "vapor",
                        "label": "VAPOR",
                        "prompt": "Vapor grade. Lighting finish only: {context}.",
                        "real": True,
                    }
                )
            )
            reloaded = client.post("/v1/styles/reload")
            self.assertEqual(reloaded.status_code, 200)
            self.assertIn("vapor", reloaded.json()["styles"])
            result = self.pipeline.process(
                image=JPEG,
                style_id="vapor",
                taken_at="2026-10-17T12:00:00Z",
                client_ip=None,
            )
        self.assertEqual(result.style_id, "vapor")
        self.assertIn("Vapor grade", result.prompt)


if __name__ == "__main__":
    unittest.main()
