#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run host contract tests with a staged SDK: python3 tests/run.py SDK."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
sdk = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="silent-remote-volume-tests-") as name:
    work = Path(name)
    api = (sdk / "rv_api.h").read_text()
    api = re.sub(r"uint32_t ([abcdef])([,)])", r"uintptr_t \1\2", api)
    (work / "rv_api.h").write_text(api)
    for file in ("osos-2.0.5.h", "rv_ui.h", "rv_sites.h"):
        (work / file).write_text((sdk / file).read_text())
    executable = work / "contracts"
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-I",
            str(work),
            str(root / "contracts.c"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)

