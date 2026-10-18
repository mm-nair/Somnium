import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from somnium_server.config import CREATIVE_PATH, load_config
from somnium_server.pipeline import Pipeline
from somnium_server.providers.prompt_openai import OpenAICompatPromptProvider
from somnium_server.styles.registry import StyleRegistry
from somnium_server.styles.schema import ALLOWED_VARIABLES, validate_style
from somnium_server.styles.template import render_template, template_variables


CREATIVE = json.loads(CREATIVE_PATH.read_text())
JPEG = b"\xff\xd8style-test\xff\xd9"


class StyleRegistryTests(unittest.TestCase):
    def test_creative_modes_stay_the_builtin_set(self):
        registry = StyleRegistry(creative=CREATIVE)
        self.assertEqual(
            list(registry),
            ["popcomic", "watercolor", "anime", "surprise", "pencil"],
        )
        pencil = registry.get("pencil")
        self.assertIn("full-color", pencil.prompt)
        self.assertEqual(pencil.source, "creative.json")
        self.assertTrue(pencil.real)
        self.assertEqual(registry.problems, [])

    def test_load_config_keeps_pipeline_fields(self):
        with patch("somnium_server.config.load_dotenv"):
            cfg = load_config()
        self.assertEqual(
            list(cfg["styles"]),
            ["popcomic", "watercolor", "anime", "surprise", "pencil"],
        )
        surprise = cfg["styles"]["surprise"]
        self.assertIn("treatment", surprise["variations"])
        self.assertTrue(surprise["liberal"])
        self.assertEqual(surprise["source"], "creative.json")

    def test_schema_rejects_unknown_variable_tint_and_tuning(self):
        with self.assertRaises(Exception) as caught:
            validate_style(
                {"id": "vapor", "prompt": "Grade it. Venue: {venue}."},
                source="test",
            )
        self.assertIn("venue", str(caught.exception))
        with self.assertRaises(Exception):
            validate_style({"id": "vapor", "tint": [256, 0, 0]}, source="test")
        with self.assertRaises(Exception):
            validate_style(
                {"id": "vapor", "tuning": {"temperature": 4, "strenght": 0.2}},
                source="test",
            )
        with self.assertRaises(Exception):
            validate_style({"id": "Vapor", "prompt": "x"}, source="test")

    def test_creative_loader_ignores_unknown_keys(self):
        mode = {
            "id": "vapor",
            "label": "VAPOR",
            "prompt": "Apply a grade. Lighting finish only: {context}.",
            "notes": "operator scribble",
        }
        style = validate_style(mode, source="creative.json", strict=False)
        self.assertEqual(style.id, "vapor")
        self.assertNotIn("notes", style.as_pipeline_dict())

    def test_user_file_overrides_and_bad_file_is_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "00pencil.json").write_text(
                json.dumps(
                    {
                        "id": "pencil",
                        "label": "PENCIL",
                        "prompt": "A custom pencil draft. Lighting finish only: {context}.",
                        "real": True,
                    }
                )
            )
            (folder / "broken.json").write_text("{")
            (folder / "typo.json").write_text(
                json.dumps({"id": "nope", "prompt": "Hello {not_a_var}."})
            )
            registry = StyleRegistry(creative=CREATIVE, styles_dir=folder)
        self.assertEqual(
            registry.get("pencil").prompt,
            "A custom pencil draft. Lighting finish only: {context}.",
        )
        self.assertEqual(registry.get("pencil").source, "file:00pencil.json")
        self.assertEqual(list(registry)[0], "popcomic")
        self.assertTrue(any("broken.json" in item for item in registry.problems))
        self.assertTrue(any("not_a_var" in item for item in registry.problems))
        self.assertNotIn("nope", registry)

    def test_plugin_and_settings_register_cyanotype(self):
        registry = StyleRegistry(
            creative=CREATIVE,
            plugins=["somnium_server.styles.plugins.cyanotype"],
        )
        style = registry.get("cyanotype")
        self.assertIsNotNone(style)
        self.assertEqual(style.tuning["temperature"], 0.35)
        self.assertIn("day_part", style.public_dict()["variables"])
        self.assertEqual(registry.problems, [])

        example = json.loads(
            (ROOT / "server/styles/examples/cyanotype.json").read_text()
        )
        again = StyleRegistry(creative=CREATIVE, user_styles=[example])
        self.assertEqual(again.get("cyanotype").source, "settings[0]")

    def test_template_variables_are_opt_in(self):
        context = {
            "day_part": "night",
            "weather": {"summary": "rain", "temp_c": 9, "wind_ms": 3},
            "astro": {"moon_phase": "full"},
            "location_label": "Planetarium",
            "artifacts": ["slightly cooler reflected light on existing surfaces"],
            "variation": {"treatment": "color slide-film photograph"},
        }
        variables = template_variables(
            style_id="cyanotype",
            style={"label": "CYANOTYPE", "keywords": "blue", "prompt": "draft", "negative": "no text"},
            context=context,
        )
        self.assertEqual(set(variables), ALLOWED_VARIABLES)
        rendered = render_template(
            "It is {day_part} and {weather}. Finish: {context}.",
            variables,
        )
        self.assertIn("night", rendered)
        self.assertIn("rain", rendered)
        self.assertIn("slightly cooler reflected light", rendered)
        self.assertNotIn("Planetarium", rendered)
        self.assertNotIn("full", rendered)
        opted = render_template("Shot at {place} under a {moon} moon.", variables)
        self.assertIn("Planetarium", opted)
        self.assertIn("full", opted)

    def test_per_style_temperature_locks_out_the_liberal_bump(self):
        context = {"artifacts": ["soft shadows"], "variation": {}}
        style = {
            "label": "CYANOTYPE",
            "keywords": "blue",
            "prompt": "Cyanotype. {context}.",
            "liberal": True,
            "tuning": {"temperature": 0.2, "top_p": 0.4, "prompt_model": "grok-3-mini"},
        }
        response = io.BytesIO(
            json.dumps({"choices": [{"message": {"content": "edited"}}]}).encode()
        )
        prompt_cfg = {
            "temperature": 0.2,
            "temperature_locked": True,
            "top_p": 0.4,
            "model": "grok-3-mini",
            "system": "refine",
        }
        with patch(
            "somnium_server.providers.prompt_openai.env_secret", return_value="test"
        ), patch(
            "somnium_server.providers.prompt_openai.urllib.request.urlopen",
            return_value=response,
        ) as urlopen:
            OpenAICompatPromptProvider().synthesize(
                style_id="cyanotype",
                style=style,
                context=context,
                prompt_cfg=prompt_cfg,
            )
        body = json.loads(urlopen.call_args.args[0].data)
        self.assertEqual(body["temperature"], 0.2)
        self.assertEqual(body["top_p"], 0.4)
        self.assertEqual(body["model"], "grok-3-mini")

    def test_unlocked_liberal_style_still_bumps_temperature(self):
        response = io.BytesIO(
            json.dumps({"choices": [{"message": {"content": "edited"}}]}).encode()
        )
        with patch(
            "somnium_server.providers.prompt_openai.env_secret", return_value="test"
        ), patch(
            "somnium_server.providers.prompt_openai.urllib.request.urlopen",
            return_value=response,
        ) as urlopen:
            OpenAICompatPromptProvider().synthesize(
                style_id="anime",
                style={"label": "ANIME", "prompt": "Anime. {context}.", "liberal": True},
                context={"artifacts": ["open midtones"]},
                prompt_cfg={"temperature": 0.45, "system": "refine"},
            )
        body = json.loads(urlopen.call_args.args[0].data)
        self.assertEqual(body["temperature"], 0.6)
        self.assertNotIn("top_p", body)

    def test_negative_and_strength_reach_the_image_prompt_only(self):
        class Recorder:
            def __init__(self):
                self.prompt = ""
                self.image_cfg = {}

            def transform(self, *, image, prompt, image_cfg):
                self.prompt = prompt
                self.image_cfg = image_cfg
                return image

        with tempfile.TemporaryDirectory() as tmp:
            cfg = {
                "server": {"output_dir": tmp},
                "styles": {
                    "cyanotype": {
                        "label": "CYANOTYPE",
                        "keywords": "blue",
                        "prompt": "Cyanotype the photo. Lighting finish only: {context}.",
                        "real": True,
                        "liberal": True,
                        "negative": "no extra people",
                        "tuning": {
                            "strength": 0.35,
                            "temperature": 0.2,
                            "image_model": "grok-imagine-image-2.0",
                        },
                        "source": "file:cyanotype.json",
                    }
                },
                "context": {"provider": "stub"},
                "prompt": {"provider": "template", "temperature": 0.45},
                "image": {"provider": "echo", "extra": {"model": "default-model"}},
                "location": {},
                "creative": {},
            }
            pipeline = Pipeline(cfg)
            recorder = Recorder()
            pipeline.stack["image"] = recorder
            result = pipeline.process(
                image=JPEG,
                style_id="cyanotype",
                taken_at="2026-10-17T12:00:00Z",
                client_ip=None,
            )
            saved = json.loads(
                Path(result.meta["orig_path"]).with_name(result.meta["saved_stem"] + ".json").read_text()
            )
        self.assertNotIn("Avoid:", result.prompt)
        self.assertNotIn("Stylization strength", result.prompt)
        self.assertIn("Avoid: no extra people", recorder.prompt)
        self.assertIn("Stylization strength 0.35", recorder.prompt)
        self.assertEqual(recorder.image_cfg["model"], "grok-imagine-image-2.0")
        self.assertTrue(recorder.image_cfg["liberal"])
        self.assertEqual(saved["negative"], "no extra people")
        self.assertEqual(saved["tuning"]["strength"], 0.35)
        self.assertIn("image_prompt", saved)

    def test_schema_file_lists_the_same_tuning_fields(self):
        schema = json.loads(
            (ROOT / "server/somnium_server/styles/style.schema.json").read_text()
        )
        self.assertEqual(
            set(schema["properties"]["tuning"]["properties"]),
            {"strength", "temperature", "top_p", "prompt_model", "image_model", "negative"},
        )
        self.assertIn("id", schema["required"])


if __name__ == "__main__":
    unittest.main()
