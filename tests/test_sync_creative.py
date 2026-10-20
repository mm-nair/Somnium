"""creative.json is the source of truth for firmware mode names and bitmaps."""

import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import sync_creative  # noqa: E402


class SyncCreativeTests(unittest.TestCase):
    def setUp(self):
        self.creative = json.loads((ROOT / "creative" / "creative.json").read_text())
        self.modes = self.creative["modes"]

    def test_generated_modes_match_creative_json(self):
        header = (ROOT / "lib" / "SomniumApp" / "Modes.h").read_text()
        self.assertEqual(header, sync_creative.modes_source(self.modes))
        self.assertEqual(
            header,
            (sync_creative.APP / "Modes.h").read_text(),
        )
        for index, mode in enumerate(self.modes):
            preview = f"kModePreview{index}" if mode.get("preview") else "nullptr"
            self.assertIn(
                f'{{"{mode["id"]}", "{mode["label"]}", ',
                header,
            )
            self.assertIn(preview, header.splitlines()[20 + index])
        self.assertIn("inline const char *labelForId", header)
        self.assertNotIn("NOIR", header)
        self.assertNotIn("CARTOON", header)
        self.assertNotIn("GOURMET", header)

    def test_theme_header_matches_creative_json(self):
        header = sync_creative.theme_source(
            self.creative.get("theme") or {},
            self.creative.get("ui") or {},
            self.creative.get("brand") or {},
        )
        self.assertEqual(header, (ROOT / "lib" / "SomniumApp" / "CreativeGen.h").read_text())
        self.assertIn('"Somnium"', header)
        self.assertIn("kitchen magic cam", header)

    def test_catalog_order_and_surprise_placeholder(self):
        self.assertEqual(
            [(mode["id"], mode["label"]) for mode in self.modes],
            [
                ("popcomic", "COMIC"),
                ("watercolor", "WATERCOLOR"),
                ("anime", "ANIME"),
                ("surprise", "SURPRISE"),
                ("pencil", "PENCIL SKETCH"),
            ],
        )
        self.assertNotIn("preview", self.modes[3])
        for mode in self.modes[:3] + self.modes[4:]:
            self.assertTrue((ROOT / "creative" / mode["preview"]).is_file())

    def test_label_drift_changes_the_header(self):
        drifted = [dict(mode) for mode in self.modes]
        drifted[0] = dict(drifted[0], label="POP")
        text = sync_creative.modes_source(drifted)
        committed = (ROOT / "lib" / "SomniumApp" / "Modes.h").read_text()
        self.assertNotEqual(text, committed)
        self.assertIn('"popcomic", "POP"', text)
        self.assertIn('"popcomic", "COMIC"', committed)

    def test_firmware_text_rules(self):
        brand = self.creative["brand"]
        self.assertTrue(sync_creative.validate_firmware_text(self.modes, brand))
        bad = [dict(mode) for mode in self.modes]
        bad[0] = dict(bad[0], label="COMIC " + "\u2014")
        self.assertFalse(sync_creative.validate_firmware_text(bad, brand))
        long_label = [dict(mode) for mode in self.modes]
        long_label[4] = dict(long_label[4], label="PENCIL SKETCH EXTRA")
        self.assertFalse(sync_creative.validate_firmware_text(long_label, brand))
        upper = [dict(mode) for mode in self.modes]
        upper[2] = dict(upper[2], id="Anime")
        self.assertFalse(sync_creative.validate_firmware_text(upper, brand))

    def test_check_command_passes(self):
        headers = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "sync_creative.py"), "--check", "--headers-only"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(headers.returncode, 0, headers.stderr)
        full = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "sync_creative.py"), "--check"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(full.returncode, 0, full.stderr)

    def test_preview_bitmaps_match_creative_json(self):
        self.assertEqual(
            sync_creative.mode_previews_source(self.modes),
            (ROOT / "lib" / "SomniumApp" / "ModeBitmaps.h").read_text(),
        )
        self.assertEqual(
            sync_creative.logo_source(self.creative.get("ui") or {}),
            (ROOT / "lib" / "SomniumApp" / "LogoBitmap.h").read_text(),
        )


if __name__ == "__main__":
    unittest.main()
