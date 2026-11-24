#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run Lyrics extension host tests: python3 tests/run.py [SDK]"""

import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent
    ext_dir = root.parent
    with tempfile.TemporaryDirectory(prefix="lyrics-tests-") as tmpdir:
        exe = Path(tmpdir) / "lrc_tests"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-pedantic-errors",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-g",
                "-fsanitize=address,undefined",
                "-I",
                str(ext_dir),
                str(root / "lrc.c"),
                str(ext_dir / "lrc.c"),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()

