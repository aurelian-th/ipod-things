#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guard the SDK version: a change to what apps build against needs a version bump.

    python3 tools/sdklock.py check    # fails when the SDK changed at the same version
    python3 tools/sdklock.py update   # records the current files and version

api.lock holds the version and the sha256 of every file an app builds against:
the headers at the root, eapp.mk, the packager, runtime/, include/, firmware/ and
.gitattributes, which decides what the tarball ships.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "api.lock"
LOCKED = (
    ".gitattributes",
    "*.h",
    "eapp.mk",
    "eapp_package.py",
    "runtime/**/*",
    "include/**/*",
    "firmware/**/*",
)


def version(sdk=None):
    text = ((sdk or ROOT) / "eapp.h").read_text()
    major = re.search(r"#define EAPP_SDK_MAJOR (\d+)", text).group(1)
    minor = re.search(r"#define EAPP_SDK_MINOR (\d+)", text).group(1)
    return f"{major}.{minor}"


def digests(sdk=None):
    sdk = sdk or ROOT
    files = sorted(
        {p for pattern in LOCKED for p in sdk.glob(pattern) if p.is_file()}
        - set(sdk.glob("**/.DS_Store"))
    )
    return {
        p.relative_to(sdk).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in files
    }


def current(sdk=None):
    return {"version": version(sdk), "sha256": digests(sdk)}


def check(sdk=None, lock=None):
    """Raise when the SDK differs from the lock without a new version."""
    recorded = json.loads((lock or LOCK).read_text())
    now = current(sdk)
    if now["sha256"] == recorded["sha256"]:
        return
    if now["version"] == recorded["version"]:
        changed = sorted(
            name
            for name in set(now["sha256"]) | set(recorded["sha256"])
            if now["sha256"].get(name) != recorded["sha256"].get(name)
        )
        raise ValueError(
            f"the SDK changed ({', '.join(changed)}) at {now['version']}: bump "
            "EAPP_SDK_MINOR or EAPP_SDK_MAJOR in eapp.h, then run "
            "tools/sdklock.py update"
        )
    raise ValueError("api.lock is stale: run tools/sdklock.py update")


def update(sdk=None, lock=None):
    (lock or LOCK).write_text(json.dumps(current(sdk), indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("action", choices=["check", "update"])
    args = parser.parse_args()
    try:
        if args.action == "check":
            check()
        else:
            update()
    except ValueError as error:
        sys.exit(str(error))


if __name__ == "__main__":
    main()
