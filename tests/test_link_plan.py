import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LinkPlanTests(unittest.TestCase):
    def test_decisions_match_the_wifi_contract(self):
        if shutil.which("g++") is None:
            self.skipTest("g++ is required")
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "link_plan"
            subprocess.run(
                [
                    "g++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "lib/Somnium"),
                    str(ROOT / "tests/link_plan_harness.cpp"),
                    str(ROOT / "lib/Somnium/LinkPlan.cpp"),
                    "-o",
                    str(executable),
                ],
                check=True,
                text=True,
            )
            result = subprocess.run([str(executable)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("link plan ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
