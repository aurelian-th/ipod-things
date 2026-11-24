# SPDX-License-Identifier: GPL-3.0-only
"""The extension SDK's audio helpers, on the host."""

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "firmware/osos-2.0.5.h"


class ExtensionSdkTests(unittest.TestCase):
    def test_audio_helpers(self):
        subprocess.run([sys.executable, ROOT / "sdk/tests/run.py", HEADER], check=True)
