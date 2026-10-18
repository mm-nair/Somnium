import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from somnium_server.eval.harness import (
    LiveEvalDisabled,
    MockImagine,
    evaluate_pair,
    evaluate_pipeline,
    live_enabled,
    restyle_jpeg,
    run_live,
    scene_jpeg,
    scramble_jpeg,
    solid_jpeg,
)
from somnium_server.eval.metrics import STRICT_THRESHOLDS
from somnium_server.eval.__main__ import main as eval_main
from somnium_server.pipeline import Pipeline


class SceneEvalTests(unittest.TestCase):
    def setUp(self):
        self.scene = scene_jpeg()

    def test_identity_passes_strict_thresholds(self):
        score = evaluate_pair(self.scene, self.scene, thresholds=STRICT_THRESHOLDS)
        self.assertGreaterEqual(score.ssim, 0.98)
        self.assertGreaterEqual(score.edge, 0.98)
        self.assertGreaterEqual(score.ahash, 0.98)
        self.assertTrue(score.passed, score.as_dict())

    def test_color_restyle_keeps_structure(self):
        score = evaluate_pair(self.scene, restyle_jpeg(self.scene))
        self.assertGreater(score.ssim, 0.7)
        self.assertGreater(score.edge, 0.7)
        self.assertGreater(score.ahash, 0.7)
        self.assertTrue(score.passed, score.as_dict())

    def test_solid_and_scramble_fail(self):
        blank = evaluate_pair(self.scene, solid_jpeg())
        scrambled = evaluate_pair(self.scene, scramble_jpeg(self.scene))
        self.assertFalse(blank.passed, blank.as_dict())
        self.assertFalse(scrambled.passed, scrambled.as_dict())
        self.assertLess(scrambled.edge, 0.35)

    def test_echo_pipeline_preserves_the_scene(self):
        with tempfile.TemporaryDirectory() as tmp:
            pipeline = _pipeline(tmp, image="echo")
            report = evaluate_pipeline(
                pipeline, self.scene, style_id="pencil", thresholds=STRICT_THRESHOLDS
            )
        self.assertTrue(report["score"]["passed"], report)

    def test_mock_imagine_preserves_the_scene_and_records_the_prompt(self):
        with tempfile.TemporaryDirectory() as tmp:
            pipeline = _pipeline(tmp, image="echo")
            mock = MockImagine()
            pipeline.stack["image"] = mock
            report = evaluate_pipeline(pipeline, self.scene, style_id="pencil")
        self.assertEqual(mock.calls, 1)
        self.assertIn("Lighting finish only", mock.prompts[0])
        self.assertTrue(report["score"]["passed"], report)
        self.assertGreater(report["score"]["histogram"], 0.5)

    def test_destructive_provider_fails_the_harness(self):
        class Blank:
            def transform(self, *, image, prompt, image_cfg):
                del image, prompt, image_cfg
                return solid_jpeg()

        with tempfile.TemporaryDirectory() as tmp:
            pipeline = _pipeline(tmp, image="echo")
            pipeline.stack["image"] = Blank()
            report = evaluate_pipeline(pipeline, self.scene, style_id="pencil")
        self.assertFalse(report["score"]["passed"], report)

    def test_live_mode_is_opt_in(self):
        previous = os.environ.pop("SOMNIUM_EVAL_LIVE", None)
        try:
            self.assertFalse(live_enabled())
            with self.assertRaises(LiveEvalDisabled):
                run_live(Path("missing.jpg"), "pencil")
            buffer = io.StringIO()
            with tempfile.TemporaryDirectory() as tmp:
                image = Path(tmp) / "scene.jpg"
                image.write_bytes(self.scene)
                from contextlib import redirect_stderr

                with redirect_stderr(buffer):
                    code = eval_main(["live", str(image), "--style", "pencil"])
            self.assertEqual(code, 2)
            self.assertIn("SOMNIUM_EVAL_LIVE", buffer.getvalue())
        finally:
            if previous is not None:
                os.environ["SOMNIUM_EVAL_LIVE"] = previous

    def test_cli_mock_and_pair(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            original = folder / "scene.jpg"
            dream = folder / "dream.jpg"
            original.write_bytes(self.scene)
            dream.write_bytes(restyle_jpeg(self.scene))
            from contextlib import redirect_stderr, redirect_stdout

            with redirect_stdout(io.StringIO()):
                self.assertEqual(eval_main(["pair", str(original), str(dream)]), 0)
                self.assertEqual(eval_main(["mock", str(original), "--style", "pencil"]), 0)
            with redirect_stderr(io.StringIO()):
                code = eval_main(["pair", str(original), str(folder / "nope.jpg")])
            self.assertEqual(code, 2)

    @unittest.skipUnless(os.environ.get("SOMNIUM_EVAL_LIVE"), "set SOMNIUM_EVAL_LIVE=1 for a live cook")
    def test_live_cook_when_explicitly_enabled(self):
        image = os.environ.get("SOMNIUM_EVAL_IMAGE")
        if not image:
            self.skipTest("set SOMNIUM_EVAL_IMAGE to a JPEG for the live cook")
        report = run_live(Path(image), os.environ.get("SOMNIUM_EVAL_STYLE", "pencil"))
        self.assertIn("score", report)
        self.assertIn("ssim", report["score"])


def _pipeline(output_dir: str, *, image: str) -> Pipeline:
    cfg = {
        "server": {"output_dir": output_dir},
        "styles": {
            "pencil": {
                "label": "PENCIL",
                "keywords": "pencil",
                "prompt": "Redraw as pencil. Lighting finish only: {context}.",
                "real": True,
            }
        },
        "context": {"provider": "stub"},
        "prompt": {"provider": "template"},
        "image": {"provider": image},
        "location": {},
        "creative": {},
    }
    return Pipeline(cfg)


if __name__ == "__main__":
    unittest.main()
