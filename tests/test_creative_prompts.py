import base64
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse
from urllib.error import HTTPError


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from somnium_server.config import CREATIVE_PATH, load_config
from somnium_server.mood_artifacts import apply_mood_artifacts
from somnium_server.pipeline import Pipeline
from somnium_server.providers.context_openmeteo import _fetch_open_meteo
from somnium_server.providers.image_xai import XaiImageProvider
from somnium_server.providers.prompt_openai import OpenAICompatPromptProvider
from somnium_server.providers.prompt_template import TemplatePromptProvider


class CreativePromptTests(unittest.TestCase):
    def setUp(self):
        self.creative = json.loads(CREATIVE_PATH.read_text())
        with patch("somnium_server.config.load_dotenv"), patch(
            "somnium_server.config._load_json",
            side_effect=lambda path: self.creative if path == CREATIVE_PATH else {},
        ):
            self.cfg = load_config()

    def mood(self, **context):
        return apply_mood_artifacts(
            context, style_id="pencil", creative=self.creative
        )

    def test_rain_and_night_each_contribute_an_accent(self):
        context = self.mood(
            day_part="night",
            weather={"summary": "rain", "cloud_cover_pct": 95},
            astro={"moon_phase": "full"},
            summary="night, rain, sky Moon, Orion",
        )
        self.assertEqual(len(context["artifacts"]), 2)
        for accent, tag in zip(context["artifacts"], ("rain", "night")):
            self.assertIn(accent, self.creative["mood_artifacts"]["pools"][tag])
            self.assertIn(accent, context["summary"])
        self.assertNotIn("full_moon", context["mood_tags"])
        self.assertNotIn("Orion", context["summary"])

    def test_astronomy_does_not_change_lighting_instructions(self):
        for day_part in ("day", "night"):
            baseline = self.mood(day_part=day_part, weather={"summary": "clear"})
            for moon_phase in ("full", "new", "waxing crescent"):
                with self.subTest(day_part=day_part, moon_phase=moon_phase):
                    context = self.mood(
                        day_part=day_part,
                        weather={"summary": "clear"},
                        astro={"moon_phase": moon_phase, "bright_objects": ["Jupiter", "Orion"]},
                    )
                    self.assertEqual(context["artifacts"], baseline["artifacts"])
                    self.assertEqual(context["mood_tags"], baseline["mood_tags"])

    def test_empty_context_does_not_invent_weather(self):
        self.assertEqual(self.mood()["artifacts"], [])

    def test_always_accents_and_zero_pick_are_respected(self):
        self.creative["mood_artifacts"]["pick"] = 0
        self.assertEqual(self.mood(day_part="day")["artifacts"], [])
        self.creative["mood_artifacts"]["always"] = ["soft ivory highlights"]
        self.assertEqual(
            self.mood(day_part="day")["artifacts"], ["soft ivory highlights"]
        )

    def test_surprise_changes_for_identical_inputs_and_saves_selections(self):
        with tempfile.TemporaryDirectory() as output_dir:
            self.cfg["server"]["output_dir"] = output_dir
            self.cfg["context"]["provider"] = "stub"
            self.cfg["prompt"]["provider"] = "template"
            self.cfg["image"]["provider"] = "echo"
            pipeline = Pipeline(self.cfg)
            previous_variation = {}
            for _ in range(12):
                result = pipeline.process(
                    image=b"offline image",
                    style_id="surprise",
                    taken_at="2026-01-01T12:00:00+00:00",
                    client_ip=None,
                )
                variation = result.context["variation"]
                self.assertEqual(set(variation), {"treatment", "palette", "focus"})
                for category, selection in variation.items():
                    self.assertNotEqual(selection, previous_variation.get(category))
                    self.assertIn(selection, result.prompt)
                saved = json.loads(
                    (Path(output_dir) / f"{result.meta['saved_stem']}.json").read_text()
                )
                self.assertEqual(saved["context"]["variation"], variation)
                previous_variation = variation

    def test_every_mode_formats_with_mood_and_pencil_stays_in_color(self):
        context = self.mood(day_part="day", weather={"summary": "clear"})
        context["variation"] = {"treatment": "color slide-film photograph"}
        for style_id, style in self.cfg["styles"].items():
            prompt = TemplatePromptProvider().synthesize(
                style_id=style_id, style=style, context=context, prompt_cfg=self.cfg["prompt"]
            )
            for accent in context["artifacts"]:
                self.assertIn(accent, prompt)
            self.assertNotIn("{context}", prompt)
            self.assertNotIn("{variation}", prompt)
        self.assertIn("full-color", self.cfg["styles"]["pencil"]["prompt"])
        self.assertIn('"pencil", "PENCIL SKETCH"', (ROOT / "lib/SomniumApp/Modes.h").read_text())

    def test_prompt_rewriter_receives_selected_variation_and_mood(self):
        context = self.mood(day_part="night", weather={"summary": "rain"})
        context["variation"] = {"treatment": "color slide-film photograph"}
        response = io.BytesIO(json.dumps({"choices": [{"message": {"content": "edited prompt"}}]}).encode())
        with patch("somnium_server.providers.prompt_openai.env_secret", return_value="test"), patch(
            "somnium_server.providers.prompt_openai.urllib.request.urlopen", return_value=response
        ) as urlopen:
            OpenAICompatPromptProvider().synthesize(
                style_id="surprise", style=self.cfg["styles"]["surprise"],
                context=context, prompt_cfg=self.cfg["prompt"],
            )
        body = json.loads(urlopen.call_args.args[0].data)
        instructions = body["messages"][1]["content"]
        self.assertIn("color slide-film photograph", instructions)
        for accent in context["artifacts"]:
            self.assertEqual(instructions.count(accent), 1)

    def test_raw_context_never_reaches_normal_or_fallback_prompts(self):
        context = {
            "summary": "night, sky Jupiter, Orion constellation, full Moon",
            "mood_watermark": "Saturn and stars",
            "astro": {"bright_objects": ["Jupiter", "Orion"]},
            "location_label": "Planetarium",
            "artifacts": ["slightly cooler shadows"],
            "variation": {"treatment": "color slide-film photograph"},
        }
        for style_id, style in self.cfg["styles"].items():
            for scenario in ("normal", "no_key", "rate_limit", "empty_response"):
                with self.subTest(style_id=style_id, scenario=scenario):
                    content = "Refined visual style." if scenario == "normal" else ""
                    response = io.BytesIO(json.dumps({"choices": [{"message": {"content": content}}]}).encode())
                    error = HTTPError("https://example.test", 429, "rate limit", {}, None)
                    with patch(
                        "somnium_server.providers.prompt_openai.env_secret",
                        return_value=None if scenario == "no_key" else "test",
                    ), patch(
                        "somnium_server.providers.prompt_openai.urllib.request.urlopen",
                        return_value=response,
                        side_effect=error if scenario == "rate_limit" else None,
                    ) as urlopen:
                        prompt = OpenAICompatPromptProvider().synthesize(
                            style_id=style_id, style=style,
                            context=context, prompt_cfg=self.cfg["prompt"],
                        )
                    error.close()
                    inspected = prompt
                    if urlopen.called:
                        inspected += urlopen.call_args.args[0].data.decode()
                    for forbidden in ("Jupiter", "Orion", "Saturn", "Planetarium", "constellation", "full Moon"):
                        self.assertNotIn(forbidden, inspected)
                    if scenario != "normal":
                        self.assertEqual(prompt.count("slightly cooler shadows"), 1)

    def test_missing_artifacts_does_not_fall_back_to_raw_sky_summary(self):
        prompt = TemplatePromptProvider().synthesize(
            style_id="anime", style=self.cfg["styles"]["anime"],
            context={"summary": "Jupiter and Orion", "mood_watermark": "full Moon"},
            prompt_cfg=self.cfg["prompt"],
        )
        self.assertIn("retain the existing lighting", prompt)
        self.assertNotIn("Jupiter", prompt)
        self.assertNotIn("Orion", prompt)
        self.assertNotIn("full Moon", prompt)

    def test_grok_request_leads_with_edit_and_allows_photographs(self):
        response = io.BytesIO(json.dumps({"data": [{"b64_json": base64.b64encode(b"image").decode()}]}).encode())
        with patch("somnium_server.providers.image_xai.env_secret", return_value="test"), patch(
            "somnium_server.providers.image_xai._jpeg_size", return_value=None
        ), patch("somnium_server.providers.image_xai._to_jpeg", return_value=b"image"), patch(
            "somnium_server.providers.image_xai.urllib.request.urlopen", return_value=response
        ) as urlopen:
            XaiImageProvider().transform(
                image=b"image", prompt="Apply a color slide-film treatment.",
                image_cfg={"liberal": True},
            )
        prompt = json.loads(urlopen.call_args.args[0].data)["prompt"]
        self.assertTrue(prompt.startswith("Apply a color slide-film treatment."))
        self.assertIn("photographic", prompt)
        self.assertNotIn("bold illustration", prompt)

    def test_openmeteo_requests_meters_per_second(self):
        with patch(
            "somnium_server.providers.context_openmeteo.urllib.request.urlopen",
            return_value=io.BytesIO(b"{}"),
        ) as urlopen:
            _fetch_open_meteo(0, 0)
        query = parse_qs(urlparse(urlopen.call_args.args[0].full_url).query)
        self.assertEqual(query["wind_speed_unit"], ["ms"])


if __name__ == "__main__":
    unittest.main()
