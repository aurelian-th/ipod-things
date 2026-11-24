# tools/tests/test_loader_lifecycle.py
# SPDX-License-Identifier: GPL-3.0-only
"""Comprehensive host verification of core extension lifecycle-hold mechanics."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "core/loader/loader.c").read_text()


def function(name):
    match = re.search(
        r"^(?:static )?[\w *]+\b" + name + r"\([^;]*?\)\n\{", SOURCE, re.M
    )
    if not match:
        raise ValueError(f"Function {name} not found in loader.c")
    start = match.start()
    at = match.end()
    depth = 1
    while depth:
        depth += (SOURCE[at] == "{") - (SOURCE[at] == "}")
        at += 1
    return SOURCE[start:at] + "\n"


PREAMBLE = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "loader/loader.h"
#include "loader/file.h"
#include "loader/rvx.h"
#include "settings.h"
#include "storage.h"
#include "ui/bitmaps.h"
#include "ui/panes.h"
#include "rv_api.h"
#define CORE_FIRMWARE_ID 0x660eb6ff
"""

DEFINITIONS = SOURCE[
    SOURCE.index("#define ROOT ") : SOURCE.index("static struct block *retiring;")
]
FOUND = SOURCE[SOURCE.index("struct found {") : SOURCE.index("/* The length of `name`")]

STUBS = r"""
/* Forward declarations of extracted and stub helpers to prevent ordering/type mismatches */
static struct block *header_of(void *memory);
static struct found *scan(uint32_t *count, uint8_t *buffer);
static int open_extension(const char *file, void **handle, struct rvx_header *header, uint8_t *failure);
static const struct rv_descriptor *enter(struct rv_extension_record *record, void *image, uint32_t entry);
static const struct manifest_module *module_record(const struct rv_extension_record *record, uint32_t index);
static void retire(void *memory);
static int load_module_now(struct rv_extension_record *record, uint32_t index);
static struct rv_extension_record *load_extension(const char *file);
static void unload_extension(struct rv_extension_record *record);
static int api_extension_hold(rv_extension_handle self);
static int api_extension_release(rv_extension_handle self);
static int try_claim_unload(struct rv_extension_record *r);
static void rescan(void);
static void unload_module(struct rv_extension_record *r, uint32_t index);

/* Real memory retirement globals declared directly after DEFINITIONS */
static struct block *retiring = NULL;
static struct block *retired = NULL;
uint32_t core_panic_extension = 0;

/* API dispatch table initialized with hold callbacks */
static struct rv_api api = {
    .extension_hold = api_extension_hold,
    .extension_release = api_extension_release,
};

struct package {
    char name[33];
    uint32_t data[400];
    uint32_t bytes, cursor;
    uint32_t body_hash;
    uint8_t duplicate;
};
static struct package packages[4];
static unsigned package_count;
static unsigned alloc_calls, free_calls, panes_refreshed, resources_retired;
static uint32_t refreshed_body_hash;
static uint32_t last_retired_owner;
static uint32_t fail_entry_owner;
static int entry_attempted_hold;
static int entry_hold_result;
static int unload_attempted_hold;
static int unload_hold_result;

static int nested_entry_called;
static int nested_hold_result;
static int outer_hold_after_nested_result;

/* IRQ depth tracking and balance validation */
static int irq_depth;
static unsigned irq_save_calls;
static unsigned irq_restore_calls;

uint32_t core_irq_save(void) {
    irq_save_calls++;
    irq_depth++;
    return 0x40;
}

void core_irq_restore(uint32_t s) {
    (void)s;
    irq_restore_calls++;
    assert(irq_depth > 0);
    irq_depth--;
}

struct setting { uint32_t owner; uint16_t key; int32_t value; };
static struct setting settings[16];
static unsigned settings_count;

uint32_t core_settings_owner(const char *s) {
    uint32_t hash = 2166136261u;
    while (*s) hash = (hash ^ (unsigned char)*s++) * 16777619u;
    return hash;
}

int32_t core_settings_get_int(uint32_t owner, uint16_t key, int32_t fallback) {
    for (unsigned i = 0; i < settings_count; i++)
        if (settings[i].owner == owner && settings[i].key == key) return settings[i].value;
    return fallback;
}

int core_settings_set_int(uint32_t owner, uint16_t key, int32_t value) {
    unsigned i = 0;
    for (; i < settings_count; i++) if (settings[i].owner == owner && settings[i].key == key) break;
    assert(i < 16); settings[i] = (struct setting){owner, key, value};
    if (i == settings_count) settings_count++;
    return 0;
}

void core_settings_flush(void) {}
static int default_on(uint32_t owner) { (void)owner; return 1; }
uint32_t core_recovery_owner(void) { return 0; }
void core_recovery_clear(void) {}

int core_file_open(const char *path, int mode, void **out) {
    assert(mode == CORE_FILE_READ);
    for (unsigned i = 0; i < package_count; i++) {
        char expected[96];
        snprintf(expected, sizeof expected, "/Revola/Extensions/%s.rvx", packages[i].name);
        if (!strcmp(path, expected)) {
            packages[i].cursor = 0;
            *out = &packages[i];
            return 1;
        }
    }
    return 0;
}

int core_file_read(void *handle, void *out, uint32_t bytes, uint32_t *got) {
    struct package *p = handle;
    if (bytes > p->bytes - p->cursor) bytes = p->bytes - p->cursor;
    memcpy(out, (char *)p->data + p->cursor, bytes);
    p->cursor += bytes;
    *got = bytes;
    return 1;
}

void core_file_close(void *h) { (void)h; }

int core_file_seek(void *handle, uint32_t at) {
    struct package *p = handle;
    if (at > p->bytes) return 0;
    p->cursor = at;
    return 1;
}

static void *allocate(struct rv_extension_record *owner, uint32_t bytes) {
    alloc_calls++;
    struct block *b = calloc(1, sizeof *b + bytes);
    assert(b);
    b->owner = owner;
    b->bytes = bytes;
    return b + 1;
}

void core_loader_charge(struct rv_extension_record *owner, int32_t bytes) {
    (void)owner;
    (void)bytes;
}

void *core_heap_allocate(struct rv_extension_record *owner, uint32_t bytes) {
    (void)owner;
    alloc_calls++;
    return calloc(1, bytes);
}

static void release(struct rv_extension_record *owner, void *p) {
    (void)owner;
    if (!p) return;
    free_calls++;
    free(header_of(p));
}

void Heap_Free(void *p) {
    if (!p) return;
    free_calls++;
    free(p);
}

void core_panes_refresh(void) {
    panes_refreshed++;
    refreshed_body_hash = core_extensions ? core_extensions->body_hash : 0;
}
void core_resource_retire(uint32_t owner) { resources_retired++; last_retired_owner = owner; }
void core_resource_tick(void) {}

static int preload_refresh_pending = 0;
uint16_t ATA_Identify[256];
int core_storage_is_flash(const uint16_t *identify) { (void)identify; return 1; }
static void reload_preloaded(struct rv_extension_record *r) { (void)r; }
static void keep_modules(struct rv_extension_record *r) { (void)r; }
static int kept(const struct rv_extension_record *r, uint32_t index) { (void)r; (void)index; return 0; }


static int read_entry(void *handle, struct rv_extension_record *record, uint32_t index, struct rvx_entry *entry) {
    (void)handle; (void)record; (void)index; (void)entry;
    return 0;
}

static int read_all(void *handle, void *out, uint32_t bytes) {
    uint32_t got = 0;
    return core_file_read(handle, out, bytes, &got) && got == bytes;
}

static int load_preloaded(struct rv_extension_record *record, void *handle) {
    (void)record; (void)handle;
    return 1;
}

static int claim_raw_sites(struct rv_extension_record *record) { (void)record; return 1; }
static int claim_content(struct rv_extension_record *record) { (void)record; return 1; }
static void drop_preloaded(struct rv_extension_record *record) { (void)record; }
void core_cache_sync(const void *start, const void *end) { (void)start; (void)end; }
static int valid_descriptor(const struct rv_descriptor *descriptor, uint32_t index) {
    (void)descriptor; (void)index;
    return 1;
}

static int bind_hooks(struct rv_extension_record *record, struct core_module *module,
                      const void *hooks, uint32_t hook_count) {
    (void)record; (void)module; (void)hooks; (void)hook_count;
    return 1;
}

int core_screens_register(struct rv_extension_record *record, uint32_t index,
                          const struct rv_screen *screens, uint32_t count) {
    (void)record; (void)index; (void)screens; (void)count;
    return RV_OK;
}

uint32_t core_screens_from_settings(const void *settings, uint32_t count, struct rv_screen *out) {
    (void)settings; (void)count; (void)out;
    return 0;
}

int core_menus_register(struct rv_extension_record *record, const void *entries, uint32_t count) {
    (void)record; (void)entries; (void)count;
    return RV_OK;
}

void core_loader_setting_changed(struct rv_extension_record *record, uint16_t key) {
    (void)record; (void)key;
}

static struct rv_descriptor test_desc;
static void test_unload_callback(void);

static const struct rv_descriptor *test_entry(const struct rv_api *api_ptr, rv_extension_handle self) {
    if (fail_entry_owner && core_panic_extension == fail_entry_owner)
        return NULL;
    entry_attempted_hold = 1;
    entry_hold_result = api_ptr->extension_hold(self);
    test_desc.unload = test_unload_callback;
    return &test_desc;
}

static const struct rv_descriptor *nested_entry_fn(const struct rv_api *api_ptr, rv_extension_handle self) {
    nested_entry_called++;
    nested_hold_result = api_ptr->extension_hold(self);
    return &test_desc;
}

static const struct rv_descriptor *outer_entry_with_nested_fn(const struct rv_api *api_ptr, rv_extension_handle self) {
    struct rv_extension_record *r = (struct rv_extension_record *)self;
    const struct rv_descriptor *nd = enter(r, (void *)(uintptr_t)nested_entry_fn, 0);
    assert(nd != NULL);
    /* Core enter was corrected to save/restore outer_entering for nested entries.
       After nested module entry returns, outer entry is still active, so entering stays 1! */
    outer_hold_after_nested_result = api_ptr->extension_hold(self);
    return &test_desc;
}

static void test_unload_callback(void) {
    unload_attempted_hold = 1;
    unload_hold_result = api.extension_hold(core_extensions);
}

static int load_module(struct rv_extension_record *r, uint32_t index) {
    (void)index;
    if (fail_entry_owner && r->owner == fail_entry_owner) {
        r->failure = CORE_EXTENSION_BAD_DESCRIPTOR;
        return 0;
    }
    void *handle;
    struct rvx_header file;
    if (!open_extension(r->file, &handle, &file, NULL)) return 0;
    r->entry_count = file.entry_count;
    r->body_hash = file.body_hash;
    core_file_close(handle);
    r->modules[0].loaded = 1;
    const struct rv_descriptor *d = enter(r, (void *)(uintptr_t)test_entry, 0);
    if (!d) {
        r->failure = CORE_EXTENSION_BAD_DESCRIPTOR;
        return 0;
    }
    return 1;
}

static void notify_unload(struct rv_extension_record *r, uint32_t index) {
    (void)r;
    (void)index;
    if (test_desc.unload) test_desc.unload();
}

static void unload_module(struct rv_extension_record *r, uint32_t index) {
    if (index < r->module_count) {
        if (r->modules[index].image) {
            release(r, r->modules[index].image);
            r->modules[index].image = NULL;
        }
        r->modules[index].loaded = 0;
    }
}

static uint32_t body_hash_of(const char *file) {
    for (unsigned i = 0; i < package_count; i++)
        if (!strcmp(packages[i].name, file)) return packages[i].body_hash;
    return 0;
}
"""

SCAN = r"""
static struct found *scan(uint32_t *count, uint8_t *buffer) {
    *count = package_count;
    struct found *list = allocate(NULL, *count * sizeof *list);
    assert(list);
    for (unsigned i = 0; i < *count; i++) {
        strcpy(list[i].file, packages[i].name);
        list[i].duplicate = packages[i].duplicate;
    }
    pick(list, *count, buffer);
    return list;
}
"""

TESTS = r"""
static void make_pkg(unsigned n, const char *name, uint32_t body_hash) {
    assert(n < 4);
    struct package *p = &packages[n];
    memset(p, 0, sizeof *p);
    strcpy(p->name, name);
    p->body_hash = body_hash;
    struct rvx_header *file = (void *)p->data;
    struct manifest_header *h = (void *)(file + 1);
    *h = (struct manifest_header){
        .magic = MANIFEST_MAGIC,
        .version = MANIFEST_VERSION,
        .api = RV_API_MAJOR << 16,
        .flags = 0,
        .module_count = 2,
        .strings_bytes = (uint32_t)strlen(name) + 1
    };
    strcpy((char *)h + tables_bytes(h), name);
    *file = (struct rvx_header){
        .magic = RVX_MAGIC,
        .format = RVX_FORMAT,
        .api = RV_API_MAJOR << 16,
        .firmware = CORE_FIRMWARE_ID,
        .entry_count = 2,
        .body_hash = body_hash,
        .manifest_bytes = tables_bytes(h) + h->strings_bytes
    };
    p->bytes = sizeof *file + file->manifest_bytes;
}

static void clean_all(void) {
    while (core_extensions) {
        struct rv_extension_record *r = core_extensions;
        core_extensions = r->next;
        free(header_of(r));
    }
    while (retiring) {
        struct block *b = retiring;
        retiring = b->next;
        free(b);
    }
    while (retired) {
        struct block *b = retired;
        retired = b->next;
        free(b);
    }
    panes_refreshed = 0;
    resources_retired = 0;
    last_retired_owner = 0;
    settings_count = 0;
    package_count = 0;
    alloc_calls = 0;
    free_calls = 0;
    core_panic_extension = 0;
    fail_entry_owner = 0;
    assert(irq_depth == 0);
}

int main(void) {
    printf("Starting Core Lifecycle-Hold Host Test Suite...\n");

    /* CASE 1: Refcount bounds, zero allocations, overflow, underflow & IRQ assertions */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL && r->holdable == 1 && r->hold_count == 0);
        assert(irq_depth == 0);

        /* Bounded underflow check */
        assert(api.extension_release(r) == RV_BAD_ARGUMENT);
        assert(r->hold_count == 0);
        assert(irq_depth == 0);

        /* Zero allocation on acquire/release */
        alloc_calls = 0;
        assert(api.extension_hold(r) == RV_OK);
        assert(alloc_calls == 0);
        assert(r->hold_count == 1);
        assert(irq_depth == 0);

        assert(api.extension_release(r) == RV_OK);
        assert(alloc_calls == 0);
        assert(r->hold_count == 0);
        assert(irq_depth == 0);

        /* Refcount overflow check at UINT16_MAX */
        r->hold_count = UINT16_MAX;
        assert(api.extension_hold(r) == RV_BUSY);
        assert(r->hold_count == UINT16_MAX);
        assert(irq_depth == 0);

        assert(api.extension_release(r) == RV_OK);
        assert(r->hold_count == UINT16_MAX - 1);
        assert(irq_depth == 0);
        r->hold_count = 0;
        printf("  [PASS] Case 1: Refcount bounds, zero-alloc, overflow/underflow, IRQ balance verified.\n");
    }

    /* CASE 2: Unknown and stale handle safety before and after 2-tick grace retirement + no ABA claims */
    {
        clean_all();
        assert(api.extension_hold((rv_extension_handle)0xdeadbeef) == RV_BAD_ARGUMENT);
        assert(api.extension_release((rv_extension_handle)0xdeadbeef) == RV_BAD_ARGUMENT);
        assert(irq_depth == 0);

        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r1 = load_extension("app1");
        assert(r1 != NULL);
        unload_extension(r1);

        /* Immediate stale handle check (before grace ticks) */
        assert(api.extension_hold(r1) == RV_BAD_ARGUMENT);
        assert(api.extension_release(r1) == RV_BAD_ARGUMENT);

        /* Grace tick 1: retiring promoted to retired */
        core_loader_tick();
        assert(api.extension_hold(r1) == RV_BAD_ARGUMENT);
        assert(api.extension_release(r1) == RV_BAD_ARGUMENT);

        /* Grace tick 2: retired freed via Heap_Free */
        core_loader_tick();
        assert(api.extension_hold(r1) == RV_BAD_ARGUMENT);
        assert(api.extension_release(r1) == RV_BAD_ARGUMENT);

        /* A later valid handle is independently holdable. */
        make_pkg(1, "app2", 0x2222);
        package_count = 2;
        struct rv_extension_record *r2 = load_extension("app2");
        assert(r2 != NULL);
        assert(api.extension_hold(r2) == RV_OK);
        assert(r2->hold_count == 1);
        assert(api.extension_release(r2) == RV_OK);
        assert(irq_depth == 0);
        printf("  [PASS] Case 2: Stale handle across 2-tick grace and valid handle acquisition verified.\n");
    }

    /* CASE 3: Entry hold forbidden & failed load hold rejection */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        entry_attempted_hold = 0;
        entry_hold_result = -999;
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL);
        assert(entry_attempted_hold == 1);
        assert(entry_hold_result == RV_BUSY);

        /* Failed load leaves holdable = 0 */
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        fail_entry_owner = core_settings_owner("app1");
        struct rv_extension_record *rf = load_extension("app1");
        assert(rf != NULL && rf->holdable == 0);
        assert(api.extension_hold(rf) == RV_BUSY);
        fail_entry_owner = 0;
        assert(irq_depth == 0);
        printf("  [PASS] Case 3: Entry hold rejection and failed load safety verified.\n");
    }

    /* CASE 4: Disable held -> tick -> release -> unload */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL);
        assert(api.extension_hold(r) == RV_OK);

        assert(core_loader_set_enabled("app1", 0) == RV_OK);
        assert(r->pending_unload == 1);
        assert(api.extension_hold(r) == RV_BUSY);

        core_loader_tick();
        assert(core_extensions == r && r->hold_count == 1);

        assert(api.extension_release(r) == RV_OK);
        assert(r->hold_count == 0);

        panes_refreshed = 0;
        core_loader_tick();
        assert(core_extensions == NULL && panes_refreshed == 1);
        assert(irq_depth == 0);
        printf("  [PASS] Case 4: Disable held, deferred tick, and release unload verified.\n");
    }

    /* CASE 5: Nested hold refcounting & staged release */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL);

        assert(api.extension_hold(r) == RV_OK);
        assert(api.extension_hold(r) == RV_OK);
        assert(api.extension_hold(r) == RV_OK);
        assert(r->hold_count == 3);

        assert(core_loader_set_enabled("app1", 0) == RV_OK);
        core_loader_tick();
        assert(core_extensions == r);

        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(core_extensions == r);

        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(core_extensions == r);

        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(core_extensions == NULL);
        assert(irq_depth == 0);
        printf("  [PASS] Case 5: Nested hold refcounting and staged release verified.\n");
    }

    /* CASE 6: Re-enable cancellation while held */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL);
        assert(api.extension_hold(r) == RV_OK);

        assert(core_loader_set_enabled("app1", 0) == RV_OK);
        assert(r->pending_unload == 1);

        assert(core_loader_set_enabled("app1", 1) == RV_OK);
        assert(r->pending_unload == 0);

        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(core_extensions == r);
        assert(irq_depth == 0);
        printf("  [PASS] Case 6: Re-enable cancellation while held verified.\n");
    }

    /* CASE 7: Unload claim atomicity (handle stays listed during unload callback) */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL);

        unload_attempted_hold = 0;
        unload_hold_result = -999;
        assert(try_claim_unload(r) == 1);
        assert(r->unloading == 1);

        unload_extension(r);
        assert(unload_attempted_hold == 1);
        assert(unload_hold_result == RV_BUSY);
        assert(api.extension_hold(r) == RV_BAD_ARGUMENT);
        assert(irq_depth == 0);
        printf("  [PASS] Case 7: Unload atomic claim and handle preservation verified.\n");
    }

    /* CASE 8: Nested module entry outer_entering save/restore & forbidden hold */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL && r->entering == 0);

        nested_entry_called = 0;
        nested_hold_result = -999;
        outer_hold_after_nested_result = -999;

        const struct rv_descriptor *d = enter(r, (void *)(uintptr_t)outer_entry_with_nested_fn, 0);
        assert(d != NULL);
        assert(nested_entry_called == 1);
        assert(nested_hold_result == RV_BUSY);
        /* Hold still forbidden after nested module entry returns because outer entry is active */
        assert(outer_hold_after_nested_result == RV_BUSY);

        assert(r->entering == 0);
        assert(api.extension_hold(r) == RV_OK);
        assert(api.extension_release(r) == RV_OK);
        assert(irq_depth == 0);
        printf("  [PASS] Case 8: Corrected enter nested save/restore & hold rejection verified.\n");
    }

    /* CASE 9: Rescan removal, duplicate, and changed body held resource retirement */
    {
        /* 9A: Removed while held */
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL);
        assert(api.extension_hold(r) == RV_OK);
        package_count = 0;
        rescan();
        assert(r->pending_unload == 1 && r->pending_rescan == 1);
        assert(core_extensions == r);
        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(core_extensions == NULL);

        /* 9B: Duplicate while held */
        clean_all();
        make_pkg(0, "copy", 0x1111);
        struct manifest_header *copy_manifest = (void *)
            ((struct rvx_header *)packages[0].data + 1);
        strcpy((char *)copy_manifest + tables_bytes(copy_manifest), "app1");
        make_pkg(1, "app1", 0x1111);
        package_count = 2;
        r = load_extension("copy");
        assert(r != NULL);
        assert(api.extension_hold(r) == RV_OK);
        rescan();
        assert(r->pending_unload == 1 && r->pending_rescan == 1);
        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();

        /* 9C: Changed body while held -> resource retirement */
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        r = load_extension("app1");
        assert(r != NULL);
        assert(api.extension_hold(r) == RV_OK);
        packages[0].body_hash = 0x2222;
        ((struct rvx_header *)packages[0].data)->body_hash = 0x2222;
        resources_retired = 0;
        rescan();
        assert(r->pending_unload == 1 && r->pending_rescan == 1);
        assert(r->pending_retire_resources == 1);
        assert(resources_retired == 0);
        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(resources_retired == 1);
        assert(last_retired_owner == r->owner);
        assert(core_extensions != NULL && core_extensions->body_hash == 0x2222);
        assert(refreshed_body_hash == 0x2222);
        assert(irq_depth == 0);
        printf("  [PASS] Case 9: Rescan removal, duplicate, and changed body retirement verified.\n");
    }

    /* CASE 10: Submodule body_hash mismatch verified with actual production load_module_now */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        assert(r != NULL && r->body_hash == 0x1111);

        packages[0].body_hash = 0x9999;
        struct rvx_header *f = (void *)packages[0].data;
        f->body_hash = 0x9999;

        assert(load_module_now(r, 1) == 0);
        assert(r->failure == CORE_EXTENSION_BAD_MODULE);
        assert(irq_depth == 0);
        printf("  [PASS] Case 10: Production load_module_now body_hash mismatch verified.\n");
    }

    /* A pending whole-extension disable still ages idle screen modules. */
    {
        clean_all();
        package_count = 1;
        make_pkg(0, "app1", 0x1111);
        struct rv_extension_record *r = load_extension("app1");
        struct manifest_module *m = (void *)(r->manifest + sizeof(struct manifest_header));
        m[1].trigger = TRIGGER_SCREEN;
        r->modules[1].loaded = 1;
        r->modules[1].idle = IDLE_TICKS - 1;
        assert(api.extension_hold(r) == RV_OK);
        assert(core_loader_set_enabled("app1", 0) == RV_OK);
        core_loader_tick();
        assert(core_extensions == r && r->modules[0].loaded);
        assert(!r->modules[1].loaded && r->hold_count == 1);
        assert(api.extension_release(r) == RV_OK);
        core_loader_tick();
        assert(core_extensions == NULL);
        printf("  [PASS] Case 11: Idle screen unload continues during held disable.\n");
    }

    clean_all();
    printf("\nALL LIFECYCLE-HOLD PREREQUISITE HOST TESTS PASSED (11/11).\n");
    return 0;
}
"""


def compile_and_run(source):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(source)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-no-pie",
                "-Wno-unused-function",
                "-Wno-unused-const-variable",
                "-Wno-pointer-to-int-cast",
                "-Wno-int-to-pointer-cast",
                "-fsanitize=address,undefined",
                "-I",
                str(ROOT / "core"),
                "-I",
                str(ROOT / "firmware"),
                "-I",
                str(ROOT / "sdk"),
                str(path / "test.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True)


class LoaderLifecycleTests(unittest.TestCase):
    def test_lifecycle_hold_all_cases(self):
        functions = "".join(
            function(name)
            for name in [
                "fnv1a",
                "copy",
                "length",
                "same_name",
                "header_of",
                "tables_bytes",
                "strings",
                "content_folder_valid",
                "manifest_valid",
                "open_extension",
                "read_manifest",
                "pick",
                "find_file",
                "core_loader_find",
                "valid_handle",
                "api_extension_hold",
                "api_extension_release",
                "try_claim_unload",
                "enter",
                "module_record",
                "retire",
                "load_module_now",
                "load_extension",
                "unload_extension",
                "load_found",
                "rescan",
                "core_loader_set_enabled",
                "core_loader_tick",
            ]
        )
        source = PREAMBLE + DEFINITIONS + FOUND + STUBS + functions + SCAN + TESTS
        compile_and_run(source)


if __name__ == "__main__":
    unittest.main()
