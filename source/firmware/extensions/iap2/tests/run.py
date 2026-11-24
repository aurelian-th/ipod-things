#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run iAP2 extension host contract and protocol tests: python3 tests/run.py [SDK]"""

import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent
    ext_dir = root.parent
    if len(sys.argv) > 1:
        sdk = Path(sys.argv[1]).resolve()
    else:
        sdk = ext_dir.parents[1] / "revola/.build/sdk"
        if not sdk.exists():
            sdk = ext_dir.parents[2] / "notes/iap2-phase1/sdk"

    with tempfile.TemporaryDirectory(prefix="iap2-tests-") as tmpdir:
        # Group 1: Existing Hook Contracts
        executable_contracts = Path(tmpdir) / "contracts"
        cmd_compile_contracts = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-I",
            str(sdk),
            "-I",
            str(ext_dir),
            str(root / "contracts.c"),
            str(ext_dir / "iap2.c"),
            str(ext_dir / "rx_tap.c"),
            str(ext_dir / "link.c"),
            str(ext_dir / "protocol.c"),
            "-o",
            str(executable_contracts),
        ]
        subprocess.run(cmd_compile_contracts, check=True)
        try:
            subprocess.run([str(executable_contracts)], check=True)
        except subprocess.CalledProcessError as exc:
            print(
                f"Contract test execution returned exit code {exc.returncode}",
                file=sys.stderr,
            )
            raise

        # Group 2: Protocol Codec and Reassembler Tests
        executable_protocol = Path(tmpdir) / "protocol_tests"
        cmd_compile_protocol = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-I",
            str(sdk),
            "-I",
            str(ext_dir),
            str(root / "protocol.c"),
            str(ext_dir / "protocol.c"),
            "-o",
            str(executable_protocol),
        ]
        subprocess.run(cmd_compile_protocol, check=True)
        try:
            subprocess.run([str(executable_protocol)], check=True)
        except subprocess.CalledProcessError as exc:
            print(
                f"Protocol test execution returned exit code {exc.returncode}",
                file=sys.stderr,
            )
            raise

        # Group 3: Link Engine Tests
        executable_link = Path(tmpdir) / "link_tests"
        subprocess.run([
            "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
            "-g", "-fsanitize=address,undefined", "-I", str(ext_dir),
            str(root / "link.c"), str(ext_dir / "link.c"),
            str(ext_dir / "protocol.c"), "-o", str(executable_link),
        ], check=True)
        subprocess.run([str(executable_link)], check=True)

        # Group 4: Receive-Only HID Tap Tests
        executable_rx_tap = Path(tmpdir) / "rx_tap_tests"
        subprocess.run([
            "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
            "-g", "-fsanitize=address,undefined", "-I", str(ext_dir),
            str(root / "rx_tap.c"), str(ext_dir / "rx_tap.c"),
            str(ext_dir / "tx.c"), str(ext_dir / "link.c"), str(ext_dir / "protocol.c"),
            "-o", str(executable_rx_tap),
        ], check=True)
        subprocess.run([str(executable_rx_tap)], check=True)

        executable_tx = Path(tmpdir) / "tx_tests"
        subprocess.run([
            "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
            "-g", "-fsanitize=address,undefined", "-I", str(ext_dir),
            str(root / "tx.c"), str(ext_dir / "tx.c"),
            str(ext_dir / "link.c"), str(ext_dir / "protocol.c"),
            "-o", str(executable_tx),
        ], check=True)
        subprocess.run([str(executable_tx)], check=True)

        executable_ctrl = Path(tmpdir) / "control_tests"
        subprocess.run([
            "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
            "-g", "-fsanitize=address,undefined", "-I", str(ext_dir),
            str(root / "control.c"), str(ext_dir / "control.c"),
            str(ext_dir / "protocol.c"),
            "-o", str(executable_ctrl),
        ], check=True)
        subprocess.run([str(executable_ctrl)], check=True)

        executable_session = Path(tmpdir) / "session_tests"
        subprocess.run([
            "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
            "-g", "-fsanitize=address,undefined", "-I", str(ext_dir),
            str(root / "session.c"), str(ext_dir / "session.c"),
            str(ext_dir / "control.c"), str(ext_dir / "link.c"),
            str(ext_dir / "protocol.c"),
            "-o", str(executable_session),
        ], check=True)
        subprocess.run([str(executable_session)], check=True)

        executable_artwork = Path(tmpdir) / "artwork_tests"
        subprocess.run([
            "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
            "-g", "-fsanitize=address,undefined", "-I", str(ext_dir),
            str(root / "artwork.c"), str(ext_dir / "artwork.c"),
            "-o", str(executable_artwork),
        ], check=True)
        subprocess.run([str(executable_artwork)], check=True)


if __name__ == "__main__":
    main()
