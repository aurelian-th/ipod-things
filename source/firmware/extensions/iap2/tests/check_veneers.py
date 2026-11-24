#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify an extension ELF contains no forbidden linker ARM/Thumb veneers.

rvx-pack only relocates words backed by R_ARM_ABS32 entries. GNU ld's
ARM<->Thumb interworking veneers load PC-relative literal pool words at address 0
without emitting R_ARM_ABS32 relocations, causing immediate crashes once the
extension is relocated in RAM.
"""

import re
import subprocess
import sys

ALLOWED_VENEERS = {"____aeabi_idiv0_from_arm"}
VENEER_RE = re.compile(r"(_from_arm|_from_thumb|veneer)", re.IGNORECASE)
STUB_SEC_RE = re.compile(r"(\.text\.__stub|veneer)", re.IGNORECASE)


def run(cmd):
    return subprocess.check_output(cmd, text=True)


def main():
    if len(sys.argv) != 3:
        print(
            "usage: check_veneers.py <cross_compile_prefix> <elf_path>",
            file=sys.stderr,
        )
        return 2

    prefix, elf = sys.argv[1], sys.argv[2]

    sec_out = run([f"{prefix}readelf", "-S", "-W", elf])
    stub_sec_indices = set()
    stub_sec_names = []
    for line in sec_out.splitlines():
        m = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)\s+", line)
        if m:
            idx, sec_name = m.group(1), m.group(2)
            if STUB_SEC_RE.search(sec_name):
                stub_sec_indices.add(idx)
                stub_sec_names.append((idx, sec_name))

    sym_out = run([f"{prefix}readelf", "-sW", elf])
    bad = {}
    for line in sym_out.splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].rstrip(":").isdigit():
            continue
        ndx = parts[6]
        if ndx == "UND":
            continue
        addr_hex = parts[1]
        name = parts[7]
        if name in ALLOWED_VENEERS:
            continue
        if VENEER_RE.search(name) or ndx in stub_sec_indices:
            addr = int(addr_hex, 16)
            bad.setdefault((addr, name), "symbol table")

    obj_out = run([f"{prefix}objdump", "-d", elf])
    in_stub_sec = False
    for line in obj_out.splitlines():
        sec_m = re.match(r"^Disassembly of section (\S+):", line)
        if sec_m:
            in_stub_sec = bool(STUB_SEC_RE.search(sec_m.group(1)))
            continue
        sym_m = re.match(r"^([0-9a-fA-F]+)\s+<([^>]+)>:", line)
        if sym_m:
            addr = int(sym_m.group(1), 16)
            name = sym_m.group(2)
            if name in ALLOWED_VENEERS:
                continue
            if VENEER_RE.search(name) or in_stub_sec:
                bad.setdefault((addr, name), "disassembly")

    if bad:
        print(
            f"ERROR: forbidden linker-generated ARM/Thumb veneer(s) in {elf}\n"
            "  rvx-pack does not relocate linker-synthesized veneer literal pools "
            "(no R_ARM_ABS32 entry is emitted), causing jumps to unrelocated "
            "address-0 targets at runtime:",
            file=sys.stderr,
        )
        for addr, name in sorted(bad):
            print(f"  0x{addr:08x} {name}", file=sys.stderr)
        return 1

    print(f"OK: 0 forbidden ARM/Thumb veneers in {elf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
