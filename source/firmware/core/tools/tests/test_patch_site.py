# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production hook patcher without firmware memory."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "core/hooks/sites.c").read_text()
FIXTURE = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hooks/sites.h"
#define TEST_MEM_BASE 0x08100000u
static uint32_t memory[8];
static struct {
    uint32_t address;
    uint32_t value;
    int writes;
} patch_record;

void core_patch_word(uint32_t address, uint32_t value)
{
    patch_record.address = address;
    patch_record.value = value;
    patch_record.writes++;
    uint32_t idx = (address - TEST_MEM_BASE) >> 2;
    assert(idx < 8);
    memory[idx] = value;
}


@FUNCTION@
int main(void)
{
    const uint32_t site_addr = TEST_MEM_BASE + 0x10; /* memory[4] */
    const uint32_t stub_addr = TEST_MEM_BASE + 0x1000;
    const uint32_t target_addr = TEST_MEM_BASE + 0x200;

    const uint32_t orig_call = BL(site_addr, target_addr);
    const uint32_t orig_tail = B(site_addr, target_addr);

    const struct core_site_row call_row = {
        .address = site_addr,
        .original = target_addr,
        .word0 = orig_call,
        .kind = CORE_SITE_CALL,
        .chain = 1,
        .argc = 1
    };

    const struct core_site_row tail_row = {
        .address = site_addr,
        .original = target_addr,
        .word0 = orig_tail,
        .kind = CORE_SITE_TAILCALL,
        .chain = 1,
        .argc = 1
    };

    /* Sentinel fill to detect adjacent memory clobbers */
    for (int i = 0; i < 8; i++) memory[i] = 0xA5A5A5A5u;
    memory[4] = orig_tail;

    /* 1. Test TAILCALL bind produces unconditional B (0xEA), NOT BL (0xEB) */
    memset(&patch_record, 0, sizeof(patch_record));
    patch_site(&tail_row, stub_addr, 1, 0);

    assert(patch_record.writes == 1);
    assert(patch_record.address == site_addr);
    assert((patch_record.value >> 24) == 0xEAu);
    assert((patch_record.value >> 24) != 0xEBu);
    assert(memory[3] == 0xA5A5A5A5u); /* word before untouched */
    assert(memory[5] == 0xA5A5A5A5u); /* word after untouched */
    assert(memory[4] == patch_record.value);

    /* Verify branch calculation */
    int32_t offset = (int32_t)(patch_record.value << 8) >> 6;
    assert(site_addr + 8 + offset == stub_addr);

    /* 2. Test TAILCALL unbind restores exact original instruction */
    patch_site(&tail_row, stub_addr, 0, 0);
    assert(memory[4] == orig_tail);
    assert(memory[3] == 0xA5A5A5A5u);
    assert(memory[5] == 0xA5A5A5A5u);

    /* 3. Test CALL bind produces BL (0xEB), NOT B (0xEA) */
    patch_site(&call_row, stub_addr, 1, 0);
    assert((patch_record.value >> 24) == 0xEBu);
    assert((patch_record.value >> 24) != 0xEAu);
    assert(memory[3] == 0xA5A5A5A5u);
    assert(memory[5] == 0xA5A5A5A5u);

    /* 4. Test CALL unbind restores word0 */
    patch_site(&call_row, stub_addr, 0, 0);
    assert(memory[4] == orig_call);

    printf("PASS: patch_site verifies B vs BL, unbind restore, and zero adjacent clobbers.\n");
    return 0;
}
"""


class PatchSiteTests(unittest.TestCase):
    def test_tail_bind_and_unbind_preserve_adjacent_literals(self):
        function = re.search(
            r"^static void patch_site\([^;]*?\n\{.*?^\}", SOURCE, re.M | re.S
        ).group(0)
        macros = "\n".join(
            line
            for line in SOURCE.splitlines()
            if line.startswith("#define BL(") or line.startswith("#define B(")
        )
        source = macros + "\n" + FIXTURE.replace("@FUNCTION@", function)
        with tempfile.TemporaryDirectory(prefix="tail-patch-") as directory:
            path = Path(directory)
            (path / "test.c").write_text(source)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "core"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
