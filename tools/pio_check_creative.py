# PlatformIO pre-script: fail the firmware build if Modes.h / CreativeGen.h
# drift from creative/creative.json. Bitmap headers are checked by
# tests/test_sync_creative.py (they need Pillow, which PlatformIO's
# interpreter may not have).
Import("env")

import os
import subprocess
import sys

root = env["PROJECT_DIR"]
script = os.path.join(root, "tools", "sync_creative.py")
print("Somnium: checking creative.json against Modes.h and CreativeGen.h")
result = subprocess.call([sys.executable, script, "--check", "--headers-only"])
if result != 0:
    print("creative.json drifted. Run ./scripts/sync_creative.sh and rebuild.")
    env.Exit(1)
