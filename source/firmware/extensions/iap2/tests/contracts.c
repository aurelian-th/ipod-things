/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "rv_api.h"
#include "rv_ui.h"
#include "../iap2.h"

#define TEST_USBHID_BYTES 0x40U

/* Fake rv_api tracking */
static uint32_t fake_refresh_calls;
static uint32_t fake_pane_refresh_calls;
static uint32_t fake_irq_depth;
static uint32_t fake_irq_save_calls;
static uint32_t fake_irq_restore_calls;
static int32_t fake_setting_protocol;
static int32_t fake_setting_diag;
static uint32_t fake_hook_next_called;
static uint32_t fake_hook_next_arg_usb;
static uint32_t fake_hook_next_arg_length;
static uint32_t fake_hook_next_arg2;
static uint32_t fake_hook_next_return_val;
static uint32_t fake_hook_next_irq_depth_at_call;
static uint32_t fake_hold_calls;
static uint32_t fake_release_calls;
static enum rv_result fake_hold_result = RV_OK;

static uint32_t test_irq_save(void)
{
    fake_irq_save_calls++;
    fake_irq_depth++;
    return fake_irq_depth;
}

static void test_irq_restore(uint32_t state)
{
    assert(fake_irq_depth > 0);
    assert(state == fake_irq_depth);
    fake_irq_depth--;
    fake_irq_restore_calls++;
}

static uint32_t test_hook_next(const void *next, uint32_t a0, uint32_t a1,
                               uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)next;
    (void)a3; (void)a4; (void)a5;
    fake_hook_next_called++;
    fake_hook_next_arg_usb = a0;
    fake_hook_next_arg_length = a1;
    fake_hook_next_arg2 = a2;
    fake_hook_next_irq_depth_at_call = fake_irq_depth;
    return fake_hook_next_return_val;
}

static int32_t test_setting_int(rv_extension_handle handle, uint16_t key)
{
    (void)handle;
    assert(fake_irq_depth == 0);
    if (key == 4)
        return fake_setting_protocol;
    if (key == 5)
        return fake_setting_diag;
    return 0;
}

static void test_refresh(rv_extension_handle handle, const char *screen)
{
    (void)handle;
    assert(fake_irq_depth == 0);
    assert(strcmp(screen, "car_audio") == 0 || strcmp(screen, "car_diag") == 0);
    if (strcmp(screen, "car_audio") == 0)
        fake_refresh_calls++;
}

static void test_pane_refresh(rv_extension_handle handle)
{
    (void)handle;
    assert(fake_irq_depth == 0);
    fake_pane_refresh_calls++;
}

static int test_extension_hold(rv_extension_handle handle)
{
    (void)handle;
    assert(fake_irq_depth == 0);
    fake_hold_calls++;
    return (int)fake_hold_result;
}

static int test_extension_release(rv_extension_handle handle)
{
    (void)handle;
    assert(fake_irq_depth == 0);
    fake_release_calls++;
    return RV_OK;
}

static const struct rv_api fake_api = {
    .size = sizeof(fake_api),
    .irq_save = test_irq_save,
    .irq_restore = test_irq_restore,
    .hook_next = test_hook_next,
    .setting_int = test_setting_int,
    .ui_refresh = test_refresh,
    .extension_hold = test_extension_hold,
    .extension_release = test_extension_release,
    .pane_refresh = test_pane_refresh,
};

/* Include production main.c directly to test production callbacks and registrations */
#include "../main.c"

static const struct iap2_hid_report_row test_hs_out_table[] = {
    { .report_id = 0x01, .reserved = 0, .body_size = 5 },
    { .report_id = 0x02, .reserved = 0, .body_size = 7 },
    { .report_id = 0x03, .reserved = 0, .body_size = 24 },
    { .report_id = 0x04, .reserved = 0, .body_size = 63 },
    { .report_id = 0x00, .reserved = 0, .body_size = 0 },
};

static const struct iap2_hid_report_row test_fs_out_table[] = {
    { .report_id = 0x01, .reserved = 0, .body_size = 5 },
    { .report_id = 0x02, .reserved = 0, .body_size = 7 },
    { .report_id = 0x03, .reserved = 0, .body_size = 24 },
    { .report_id = 0x00, .reserved = 0, .body_size = 0 },
};

static const struct iap2_hid_report_row *test_out_report_table(uint8_t hs)
{
    return hs ? test_hs_out_table : test_fs_out_table;
}

typedef uint32_t (*hid_activate_hook_fn)(uint8_t *hid, uint32_t high_speed, const void *next);
typedef uint32_t (*hid_deactivate_hook_fn)(uint8_t *hid, const void *next);
typedef uint32_t (*hid_ctrl_write_hook_fn)(uint8_t *hid, const uint8_t *setup, uint32_t received, const void *next);

static void init_fake_hid(uint8_t *hid, uint8_t active, uint8_t hs,
                          uint16_t hs_count, uint16_t fs_count,
                          const uint8_t *out_buf, uint32_t out_cap)
{
    memset(hid, 0, TEST_USBHID_BYTES);
    hid[USBHID_active] = active;
    uint8_t *rmap = hid + USBHID_report_map;
    rmap[USBReportMap_speed] = hs;
    rmap[USBReportMap_hs_out_count] = (uint8_t)(hs_count & 0xFFU);
    rmap[USBReportMap_hs_out_count + 1] = (uint8_t)(hs_count >> 8);
    rmap[USBReportMap_fs_out_count] = (uint8_t)(fs_count & 0xFFU);
    rmap[USBReportMap_fs_out_count + 1] = (uint8_t)(fs_count >> 8);
    memcpy(hid + USBHID_out_buffer, &out_buf, sizeof(out_buf));
    memcpy(hid + USBHID_out_capacity, &out_cap, sizeof(out_cap));
}

static void make_set_report_setup(uint8_t setup[8], uint8_t req_type, uint8_t req,
                                  uint8_t report_type, uint8_t report_id, uint16_t len)
{
    setup[0] = req_type;
    setup[1] = req;
    setup[2] = report_id;
    setup[3] = report_type;
    setup[4] = 0x02;
    setup[5] = 0x00;
    setup[6] = (uint8_t)(len & 0xFFU);
    setup[7] = (uint8_t)(len >> 8);
}

static uint8_t checksum_twos(const uint8_t *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += buf[i];
    return (uint8_t)((0x100U - (sum & 0xFFU)) & 0xFFU);
}

static void test_descriptor_and_iap1_passthrough(void)
{
    fake_setting_protocol = IAP2_MODE_IAP1;
    fake_hold_result = RV_OK;
    fake_hold_calls = 0;
    fake_release_calls = 0;
    fake_refresh_calls = 0;
    fake_pane_refresh_calls = 0;

    rv_extension_handle fake_handle = (rv_extension_handle)0x1234;
    const struct rv_descriptor *desc = rv_module_entry(&fake_api, fake_handle);
    assert(desc != NULL);
    assert(desc->hook_count == 3);
    assert(desc->setting_count == 2);
    assert(desc->screen_count == 3);
    assert(desc->entry_count == 1);

    assert(desc->hooks[0].site == RV_SITE_USBHID_Activate_slot);
    assert(desc->hooks[1].site == RV_SITE_USBHID_Deactivate_slot);
    assert(desc->hooks[2].site == RV_SITE_USBHID_HandleControlWrite_slot);

    assert(desc->settings[0].key == KEY_PROTOCOL_MODE);
    assert(desc->settings[0].type == RV_SETTING_TYPE_INT);
    assert(desc->settings[0].style == RV_SETTING_STYLE_NONE);
    assert(desc->settings[0].fallback == IAP2_MODE_IAP2);
    assert(desc->settings[0].minimum == IAP2_MODE_IAP1);
    assert(desc->settings[0].maximum == IAP2_MODE_IAP2);
    assert(desc->settings[0].label_count == 2);
    assert(strcmp(desc->settings[0].labels[0], "iAP1") == 0);
    assert(strcmp(desc->settings[0].labels[1], "iAP2") == 0);

    assert(desc->settings[1].key == KEY_DIAG_LOGGING);
    assert(desc->settings[1].type == RV_SETTING_TYPE_INT);
    assert(desc->settings[1].fallback == 0);

    assert(strcmp(desc->screens[0].id, "car_audio") == 0);
    assert(strcmp(desc->screens[0].title, "Car Audio") == 0);
    assert(desc->screens[0].kind == RV_SCREEN_KIND_LIST);
    assert(desc->screens[0].list.count == 5);
    assert(desc->screens[0].list.panes != NULL);

    assert(strcmp(desc->screens[1].id, "car_diag") == 0);
    assert(strcmp(desc->screens[1].title, "Diagnostics") == 0);
    assert(desc->screens[1].kind == RV_SCREEN_KIND_LIST);
    assert(desc->screens[1].list.count == 6);

    assert(strcmp(desc->screens[2].id, "car_about") == 0);
    assert(strcmp(desc->screens[2].title, "Car Audio") == 0);
    assert(desc->screens[2].kind == RV_SCREEN_KIND_TEXT);
    assert(desc->screens[2].text.count == 4);

    assert(desc->entries[0].menu == RV_MENU_SETTINGS);
    assert(desc->entries[0].after == RV_SETTINGS_SOUND_CHECK);
    assert(desc->entries[0].row.kind == RV_ROW_KIND_PUSH);
    assert(strcmp(desc->entries[0].row.label, "Car Audio") == 0);
    assert(strcmp(desc->entries[0].row.screen, "car_audio") == 0);
    assert(desc->entries[0].bitmap != NULL);
    assert(strcmp(desc->entries[0].bitmap->asset, "settings-car") == 0);
    assert(desc->entries[0].caption_text != NULL);

    desc->setting_changed(RV_SETTING_KEY_ALL);
    assert(diag_state.protocol_mode == IAP2_MODE_IAP1);
    assert(diag_state.tap_desired == 0);
    assert(fake_refresh_calls == 1);
    assert(fake_pane_refresh_calls == 1);

    hid_activate_hook_fn hid_act = (hid_activate_hook_fn)desc->hooks[0].handler;
    hid_deactivate_hook_fn hid_deact = (hid_deactivate_hook_fn)desc->hooks[1].handler;
    hid_ctrl_write_hook_fn hid_cw = (hid_ctrl_write_hook_fn)desc->hooks[2].handler;

    _Alignas(const void *) uint8_t hid[TEST_USBHID_BYTES];
    static const uint8_t probe_bytes[6] = { 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10 };
    uint8_t out_buf[64] = { 0x02, 0x00 };
    memcpy(out_buf + 2, probe_bytes, sizeof(probe_bytes));
    init_fake_hid(hid, 1, 1, 4, 3, out_buf, sizeof(out_buf));

    uint8_t setup[8];
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x02, 8);

    fake_hook_next_called = 0;
    fake_hook_next_return_val = 1;
    assert(hid_act(hid, 1, NULL) == 1);
    assert(fake_hook_next_called == 1);
    assert(fake_hold_calls == 0);
    assert(diag_state.usb_connected == 1);
    assert(diag_state.tap_active == 0);
    assert(diag_state.session_active == 0);

    /* Even an iAP2 probe passes straight through in iAP1 mode */
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 42;
    assert(hid_cw(hid, setup, 8, NULL) == 42);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 0);

    fake_hook_next_return_val = 0;
    assert(hid_deact(hid, NULL) == 0);
    assert(fake_release_calls == 0);
    assert(diag_state.usb_connected == 0);

    puts("  [PASS] Car Audio descriptor & iAP1 pass-through verified");
}

static void test_iap2_mode_lifecycle_and_replug(void)
{
    fake_setting_protocol = IAP2_MODE_IAP1;
    fake_hold_calls = 0;
    fake_release_calls = 0;
    fake_hold_result = RV_OK;

    const struct rv_descriptor *desc = rv_module_entry(&fake_api, (rv_extension_handle)0x1234);
    hid_activate_hook_fn hid_act = (hid_activate_hook_fn)desc->hooks[0].handler;
    hid_deactivate_hook_fn hid_deact = (hid_deactivate_hook_fn)desc->hooks[1].handler;

    _Alignas(const void *) uint8_t hid_a[TEST_USBHID_BYTES];
    _Alignas(const void *) uint8_t hid_b[TEST_USBHID_BYTES];
    uint8_t out_buf[64] = {0};
    init_fake_hid(hid_a, 1, 1, 4, 3, out_buf, sizeof(out_buf));
    init_fake_hid(hid_b, 1, 0, 4, 3, out_buf, sizeof(out_buf));

    /* Switch to iAP2 mode */
    fake_setting_protocol = IAP2_MODE_IAP2;
    desc->setting_changed(KEY_PROTOCOL_MODE);
    assert(diag_state.protocol_mode == IAP2_MODE_IAP2);
    assert(diag_state.tap_desired == 1);
    assert(strcmp(desc->screens[0].list.rows[1].text(NULL), "Ready (iAP2)") == 0);

    /* 1. Native failure (res == 0) must not acquire hold */
    fake_hook_next_return_val = 0;
    assert(hid_act(hid_a, 1, NULL) == 0);
    assert(fake_hold_calls == 0);
    assert(diag_state.hold_acquired == 0);
    assert(diag_state.session_active == 0);

    /* 2. Native inactive (USBHID_active == 0) must not acquire hold */
    hid_a[USBHID_active] = 0;
    fake_hook_next_return_val = 1;
    assert(hid_act(hid_a, 1, NULL) == 1);
    assert(fake_hold_calls == 0);
    assert(diag_state.hold_acquired == 0);

    /* 3. Refused hold (RV_BUSY) records refusal and leaves tap inactive */
    hid_a[USBHID_active] = 1;
    fake_hold_result = RV_BUSY;
    assert(hid_act(hid_a, 1, NULL) == 1);
    assert(fake_hold_calls == 1);
    assert(fake_release_calls == 0);
    assert(diag_state.hold_refused_count == 1);
    assert(diag_state.hold_acquired == 0);
    assert(diag_state.tap_active == 0);
    assert(diag_state.session_active == 0);

    /* 4. Successful hold activation */
    fake_hold_result = RV_OK;
    assert(hid_act(hid_a, 1, NULL) == 1);
    assert(fake_hold_calls == 2);
    assert(diag_state.usb_connected == 1);
    assert(diag_state.hold_acquired == 1);
    assert(diag_state.hold_acquired_count == 1);
    assert(diag_state.tap_active == 1);
    assert(diag_state.session_active == 1);
    assert(diag_state.session_gen == 1);
    assert(active_hid_cookie == (uintptr_t)hid_a);

    /* 5. Re-activate while already held and desired == 1 reuses existing hold */
    assert(hid_act(hid_a, 1, NULL) == 1);
    assert(fake_hold_calls == 2);
    assert(diag_state.hold_acquired_count == 1);
    assert(diag_state.session_gen == 2);

    /* 6. Deactivate with mismatched cookie does not touch session or hold */
    assert(hid_deact(hid_b, NULL) == 1);
    assert(fake_release_calls == 0);
    assert(diag_state.hold_acquired == 1);
    assert(diag_state.session_active == 1);

    /* 7. Mid-session switch iAP2 -> iAP1 latches until disconnect / re-activate */
    fake_setting_protocol = IAP2_MODE_IAP1;
    desc->setting_changed(KEY_PROTOCOL_MODE);
    assert(diag_state.protocol_mode == IAP2_MODE_IAP1);
    assert(diag_state.tap_desired == 0);
    assert(diag_state.tap_active == 1);
    assert(diag_state.hold_acquired == 1);
    assert(strcmp(desc->screens[0].list.rows[1].text(NULL), "Replug USB") == 0);
    assert(strcmp(desc->screens[0].list.panes[1].caption_text(NULL), "Replug to switch") == 0);

    assert(hid_act(hid_a, 1, NULL) == 1);
    assert(fake_release_calls == 1);
    assert(diag_state.hold_acquired == 0);
    assert(diag_state.session_active == 0);
    assert(diag_state.tap_active == 0);
    assert(diag_state.usb_connected == 1);
    assert(active_hid_cookie == (uintptr_t)hid_a);
    assert(diag_state.session_gen == 3);
    assert(strcmp(desc->screens[0].list.rows[1].text(NULL), "Classic (iAP1)") == 0);

    /* 8. Mid-session switch iAP1 -> iAP2 while plugged in prompts replug until deactivate */
    fake_setting_protocol = IAP2_MODE_IAP2;
    desc->setting_changed(KEY_PROTOCOL_MODE);
    assert(strcmp(desc->screens[0].list.rows[1].text(NULL), "Replug USB") == 0);

    fake_hook_next_return_val = 0;
    assert(hid_deact(hid_a, NULL) == 0);
    assert(diag_state.usb_connected == 0);
    assert(active_hid_cookie == 0);
    assert(strcmp(desc->screens[0].list.rows[1].text(NULL), "Ready (iAP2)") == 0);

    /* 9. Fresh activate in iAP2, then deactivate with matching cookie */
    fake_hook_next_return_val = 1;
    assert(hid_act(hid_a, 1, NULL) == 1);
    assert(fake_hold_calls == 3);
    assert(diag_state.hold_acquired == 1);
    assert(diag_state.session_gen == 5);

    fake_hook_next_return_val = 0;
    assert(hid_deact(hid_a, NULL) == 0);
    assert(fake_release_calls == 2);
    assert(diag_state.hold_acquired == 0);
    assert(diag_state.session_active == 0);
    assert(diag_state.tap_active == 0);
    assert(diag_state.usb_connected == 0);
    assert(active_hid_cookie == 0);
    assert(diag_state.session_gen == 6);

    puts("  [PASS] HID lifecycle hold, refusal, replug prompt, and deactivate rollback verified");
}

static void test_hid_control_write_tap(void)
{
    fake_setting_protocol = IAP2_MODE_IAP2;
    fake_hold_result = RV_OK;
    fake_hold_calls = 0;
    fake_release_calls = 0;

    const struct rv_descriptor *desc = rv_module_entry(&fake_api, (rv_extension_handle)0x1234);
    desc->setting_changed(RV_SETTING_KEY_ALL);

    hid_activate_hook_fn hid_act = (hid_activate_hook_fn)desc->hooks[0].handler;
    hid_deactivate_hook_fn hid_deact = (hid_deactivate_hook_fn)desc->hooks[1].handler;
    hid_ctrl_write_hook_fn hid_cw = (hid_ctrl_write_hook_fn)desc->hooks[2].handler;

    _Alignas(const void *) uint8_t hid[TEST_USBHID_BYTES];
    uint8_t out_buf[128];
    memset(out_buf, 0, sizeof(out_buf));
    init_fake_hid(hid, 1, 1, 4, 3, out_buf, sizeof(out_buf));

    fake_hook_next_return_val = 1;
    assert(hid_act(hid, 1, NULL) == 1);
    assert(diag_state.tap_active == 1);

    uint8_t setup[8];

    /* 1. Non-Output SET_REPORT (e.g. Feature report type 3) passes through untouched */
    make_set_report_setup(setup, 0x21, 0x09, 0x03, 0x01, 6);
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0;
    assert(hid_cw(hid, setup, 6, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 0);

    /* 2. Malformed length (< 2 and > out_cap) counts bad report and forwards */
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 1);
    fake_hook_next_called = 0;
    fake_hook_next_return_val = (uint32_t)-5;
    assert(hid_cw(hid, setup, 1, NULL) == (uint32_t)-5);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 1);
    assert(diag_state.shared_rx_stats.rx_bad_report == 1);

    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 200);
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0;
    assert(hid_cw(hid, setup, 200, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 2);
    assert(diag_state.shared_rx_stats.rx_bad_report == 2);

    /* 3. Report ID mismatch between wValue low byte and out_buf[0] */
    out_buf[0] = 0x02;
    out_buf[1] = 0x00;
    out_buf[2] = 0x11;
    out_buf[3] = 0x22;
    out_buf[4] = 0x33;
    out_buf[5] = 0x44;
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 6);
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 6, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 3);
    assert(diag_state.shared_rx_stats.rx_bad_report == 3);

    /* 4. Valid iAP1 report passes through to native handler */
    out_buf[0] = 0x01;
    out_buf[1] = 0x00;
    out_buf[2] = 0x01;
    out_buf[3] = 0x02;
    out_buf[4] = 0x03;
    out_buf[5] = 0x04;
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 6);
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0;
    assert(hid_cw(hid, setup, 6, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 4);
    assert(diag_state.shared_rx_stats.rx_iap1_passthrough == 1);

    /* 5. Valid iAP2 SYN packet in Full-Speed report row 0x03 is claimed and ACKed (0) */
    init_fake_hid(hid, 1, 0, 4, 3, out_buf, sizeof(out_buf));
    uint8_t syn_pkt[23] = {
        0xFF, 0x5A, 0x00, 0x17, 0x80, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x04, 0x00, 0x03, 0xE8, 0x00, 0x64, 0x03, 0x01,
        0x0A, 0x00, 0x01, 0x00
    };
    syn_pkt[8] = checksum_twos(syn_pkt, 8);
    syn_pkt[22] = checksum_twos(syn_pkt + 9, 13);

    out_buf[0] = 0x03;
    out_buf[1] = 0x00;
    memcpy(out_buf + 2, syn_pkt, sizeof(syn_pkt));
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x03, 25);

    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0xdeadbeef;
    assert(hid_cw(hid, setup, 25, NULL) == 0U);
    assert(fake_hook_next_called == 0);
    assert(diag_state.shared_rx_stats.rx_total_reports == 5);
    assert(diag_state.shared_rx_stats.rx_iap2_packets == 1);

    /* 5b. Probe, fragmented iAP1, and malformed DROP_ERROR via hook */
    static const uint8_t probe_bytes[6] = { 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10 };
    out_buf[0] = 0x02;
    out_buf[1] = 0x00;
    memcpy(out_buf + 2, probe_bytes, sizeof(probe_bytes));
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x02, 8);
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 8, NULL) == 0U);
    assert(fake_hook_next_called == 0);
    assert(diag_state.shared_rx_stats.rx_iap2_probes == 1);

    /* Fragmented iAP1 (MORE_TO_FOLLOW then CONTINUE) passes through to native */
    out_buf[0] = 0x01;
    out_buf[1] = IAP2_LINK_CONTROL_MORE_TO_FOLLOW;
    out_buf[2] = 0xFF; out_buf[3] = 0x55; out_buf[4] = 0x10; out_buf[5] = 0x04;
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 6);
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0;
    assert(hid_cw(hid, setup, 6, NULL) == 0);
    assert(fake_hook_next_called == 1);
    out_buf[1] = IAP2_LINK_CONTROL_CONTINUE;
    out_buf[2] = 0xAA; out_buf[3] = 0xBB;
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 6, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_iap1_passthrough == 3);

    /* Malformed iAP2 checksum drops with ACK (0), NOT forwarded to native */
    syn_pkt[8] ^= 0xFF;
    out_buf[0] = 0x03;
    out_buf[1] = 0x00;
    memcpy(out_buf + 2, syn_pkt, sizeof(syn_pkt));
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x03, 25);
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0xdeadbeef;
    assert(hid_cw(hid, setup, 25, NULL) == 0U);
    assert(fake_hook_next_called == 0);
    assert(diag_state.shared_rx_stats.rx_bad_checksum == 1);

    /* Mismatched hid cookie and wrong bRequest pass through without counting */
    _Alignas(const void *) uint8_t other_hid[TEST_USBHID_BYTES];
    init_fake_hid(other_hid, 1, 1, 4, 3, out_buf, sizeof(out_buf));
    uint32_t prev_total = diag_state.shared_rx_stats.rx_total_reports;
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 7;
    assert(hid_cw(other_hid, setup, 25, NULL) == 7);
    assert(fake_hook_next_called == 1);
    make_set_report_setup(setup, 0x21, 0x0A, 0x02, 0x03, 25);
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 25, NULL) == 7);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == prev_total);

    /* 6. Deactivate cleanly releases hold */
    fake_hook_next_return_val = 0;
    assert(hid_deact(hid, NULL) == 0);
    assert(fake_release_calls == 1);
    assert(diag_state.session_active == 0);

    puts("  [PASS] HID control write tap (iAP1 passthrough, iAP2 claim/ACK, bounds, embedded report_map) verified");
}

static void test_unload_concurrency_safety(void)
{
    fake_setting_protocol = IAP2_MODE_IAP2;
    fake_hold_result = RV_OK;
    fake_hold_calls = 0;
    fake_release_calls = 0;

    const struct rv_descriptor *desc = rv_module_entry(&fake_api, (rv_extension_handle)0x1234);
    desc->setting_changed(RV_SETTING_KEY_ALL);

    hid_activate_hook_fn hid_act = (hid_activate_hook_fn)desc->hooks[0].handler;
    hid_deactivate_hook_fn hid_deact = (hid_deactivate_hook_fn)desc->hooks[1].handler;
    hid_ctrl_write_hook_fn hid_cw = (hid_ctrl_write_hook_fn)desc->hooks[2].handler;

    _Alignas(const void *) uint8_t hid[TEST_USBHID_BYTES];
    uint8_t out_buf[64] = {0x01, 0x00, 0x11, 0x22, 0x33, 0x44};
    init_fake_hid(hid, 1, 1, 4, 3, out_buf, sizeof(out_buf));
    fake_hook_next_return_val = 1;
    assert(hid_act(hid, 1, NULL) == 1);
    assert(diag_state.hold_acquired == 1);

    /* Call unload under IRQ lock */
    fake_irq_save_calls = 0;
    fake_irq_restore_calls = 0;
    desc->unload();
    assert(fake_irq_save_calls == 1);
    assert(fake_irq_restore_calls == 1);
    assert(diag_state.usb_connected == 0);
    assert(diag_state.tap_desired == 0);
    assert(diag_state.tap_active == 0);
    assert(diag_state.session_active == 0);

    /* Subsequent control write while hooks await retirement passes through */
    uint8_t setup[8];
    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 6);
    fake_hook_next_called = 0;
    hid_cw(hid, setup, 6, NULL);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_total_reports == 0);

    /* Pending deactivate after unload still releases held ref */
    assert(hid_deact(hid, NULL) == 1);
    assert(fake_release_calls == 1);
    assert(diag_state.hold_acquired == 0);

    puts("  [PASS] Unload disable under IRQ & post-unload pass-through verified for all 3 HID hooks");
}

static void test_malformed_inputs_on_production_hooks(void)
{
    fake_setting_protocol = IAP2_MODE_IAP2;
    fake_hold_result = RV_OK;

    const struct rv_descriptor *desc = rv_module_entry(&fake_api, (rv_extension_handle)0x1234);
    desc->setting_changed(RV_SETTING_KEY_ALL);

    hid_activate_hook_fn hid_act = (hid_activate_hook_fn)desc->hooks[0].handler;
    hid_deactivate_hook_fn hid_deact = (hid_deactivate_hook_fn)desc->hooks[1].handler;
    hid_ctrl_write_hook_fn hid_cw = (hid_ctrl_write_hook_fn)desc->hooks[2].handler;

    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0;
    assert(hid_act(NULL, 1, NULL) == 0);
    assert(fake_hook_next_called == 1);

    fake_hook_next_called = 0;
    assert(hid_deact(NULL, NULL) == 0);
    assert(fake_hook_next_called == 1);

    fake_hook_next_called = 0;
    assert(hid_cw(NULL, NULL, 0, NULL) == 0);
    assert(fake_hook_next_called == 1);

    /* Activate valid HID and verify malformed SET_REPORT setups/lengths forward cleanly */
    _Alignas(const void *) uint8_t hid[TEST_USBHID_BYTES];
    uint8_t out_buf[64] = {0x02, 0x00, 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10};
    init_fake_hid(hid, 1, 1, 4, 3, out_buf, sizeof(out_buf));
    fake_hook_next_return_val = 1;
    assert(hid_act(hid, 1, NULL) == 1);

    uint8_t setup[8];
    make_set_report_setup(setup, 0x21, 0x09, 0x01, 0x02, 8); /* Input report type 1 */
    fake_hook_next_called = 0;
    fake_hook_next_return_val = 0;
    assert(hid_cw(hid, setup, 8, NULL) == 0);
    assert(fake_hook_next_called == 1);

    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x02, 1); /* Too short */
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 1, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_bad_report == 1);

    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x02, 200); /* Exceeds out_cap */
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 200, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_bad_report == 2);

    make_set_report_setup(setup, 0x21, 0x09, 0x02, 0x01, 8); /* Report ID mismatch */
    fake_hook_next_called = 0;
    assert(hid_cw(hid, setup, 8, NULL) == 0);
    assert(fake_hook_next_called == 1);
    assert(diag_state.shared_rx_stats.rx_bad_report == 3);

    assert(hid_deact(hid, NULL) == 0);

    puts("  [PASS] Malformed NULL, length, type, and ID inputs on all 3 HID hooks verified");
}

static void test_consumer_ui_formatters_and_callbacks(void)
{
    /* NULL snapshot defaults */
    assert(strcmp(iap2_ui_settings_caption(NULL), "iAP2 (Newer cars)") == 0);
    assert(strcmp(iap2_ui_protocol_caption(NULL), "Newer cars (USB)") == 0);
    assert(strcmp(iap2_ui_status_row(NULL), "Ready (iAP2)") == 0);
    assert(strcmp(iap2_ui_status_caption(NULL), "Waiting for car") == 0);

    struct iap2_diag_snapshot snap;
    iap2_diag_snapshot(NULL, &snap);

    /* iAP1 idle */
    snap.protocol_mode = IAP2_MODE_IAP1;
    snap.tap_desired = 0;
    snap.tx_desired = 0;
    assert(strcmp(iap2_ui_settings_caption(&snap), "iAP1 (Older cars)") == 0);
    assert(strcmp(iap2_ui_protocol_caption(&snap), "Older cars & docks") == 0);
    assert(strcmp(iap2_ui_status_row(&snap), "Classic (iAP1)") == 0);
    assert(strcmp(iap2_ui_status_caption(&snap), "Stock iAP1 active") == 0);

    /* iAP2 idle */
    snap.protocol_mode = IAP2_MODE_IAP2;
    snap.tap_desired = 1;
    snap.tx_desired = 1;
    assert(strcmp(iap2_ui_settings_caption(&snap), "iAP2 (Newer cars)") == 0);
    assert(strcmp(iap2_ui_protocol_caption(&snap), "Newer cars (USB)") == 0);
    assert(strcmp(iap2_ui_status_row(&snap), "Ready (iAP2)") == 0);
    assert(strcmp(iap2_ui_status_caption(&snap), "Waiting for car") == 0);

    /* TX connecting */
    snap.usb_connected = 1;
    snap.tap_active = 1;
    snap.tx_active = 1;
    snap.session_summary.phase = IAP2_SESSION_PHASE_AUTH_WAIT;
    assert(strcmp(iap2_ui_status_row(&snap), "Connecting...") == 0);
    assert(strcmp(iap2_ui_status_caption(&snap), "Connecting...") == 0);

    /* TX ready without audio */
    snap.session_summary.phase = IAP2_SESSION_PHASE_READY;
    snap.session_summary.audio_active = false;
    assert(strcmp(iap2_ui_settings_caption(&snap), "iAP2 Connected") == 0);
    assert(strcmp(iap2_ui_status_row(&snap), "Connected") == 0);
    assert(strcmp(iap2_ui_status_caption(&snap), "Car connected") == 0);

    /* TX ready with 44.1 kHz USB audio */
    snap.session_summary.audio_active = true;
    snap.session_summary.audio_rate = 44100U;
    assert(strcmp(iap2_ui_settings_caption(&snap), "iAP2 Connected") == 0);
    assert(strcmp(iap2_ui_status_row(&snap), "Connected (44k)") == 0);
    assert(strcmp(iap2_ui_status_caption(&snap), "USB audio active") == 0);

    /* Replug prompt */
    snap.protocol_mode = IAP2_MODE_IAP1;
    snap.tap_desired = 0;
    snap.tx_desired = 0;
    assert(strcmp(iap2_ui_status_row(&snap), "Replug USB") == 0);
    assert(strcmp(iap2_ui_status_caption(&snap), "Replug to switch") == 0);

    /* Verify UI callbacks take IRQ-masked snapshots cleanly */
    fake_setting_protocol = IAP2_MODE_IAP2;
    const struct rv_descriptor *desc = rv_module_entry(&fake_api, (rv_extension_handle)0x1234);
    desc->setting_changed(RV_SETTING_KEY_ALL);

    fake_irq_save_calls = 0;
    fake_irq_restore_calls = 0;
    assert(strcmp(desc->entries[0].caption_text(NULL), "iAP2 (Newer cars)") == 0);
    assert(strcmp(desc->screens[0].list.panes[0].caption_text(NULL), "Newer cars (USB)") == 0);
    assert(strcmp(desc->screens[0].list.rows[1].text(NULL), "Ready (iAP2)") == 0);
    assert(strcmp(desc->screens[0].list.panes[1].caption_text(NULL), "Waiting for car") == 0);
    assert(strcmp(desc->screens[0].list.panes[2].caption_text(NULL), "Off (no disk writes)") == 0);
    assert(strcmp(desc->screens[0].list.panes[4].caption, "Older vs. newer") == 0);
    assert(fake_irq_save_calls == 5);
    assert(fake_irq_restore_calls == 5);

    fake_setting_diag = 1;
    desc->setting_changed(KEY_DIAG_LOGGING);
    assert(diag_state.diag_logging == 1);
    assert(strcmp(desc->screens[0].list.panes[2].caption_text(NULL), "Writes Data/.../diag.txt") == 0);
    assert(strcmp(desc->screens[1].list.rows[5].text(NULL), "Data/.../iap2/diag.txt") == 0);

    struct iap2_diag_snapshot diag_snap;
    read_ui_snapshot(&diag_snap);
    diag_snap.session_summary.car_manufacturer[0] = 'V';
    diag_snap.session_summary.car_manufacturer[1] = 'W';
    diag_snap.session_summary.car_manufacturer[2] = '\0';
    diag_snap.session_summary.play_cmd_suppressed = 3U;
    char report[1024];
    size_t rlen = iap2_format_diag_report(&diag_snap, report, sizeof(report));
    assert(rlen > 0U);
    assert(strstr(report, "RevisionOS Car Audio v1.1.0") != NULL);
    assert(strstr(report, "Mfr:VW") != NULL);
    assert(strstr(report, "Suppressed:3") != NULL);

    fake_setting_diag = 0;
    desc->setting_changed(KEY_DIAG_LOGGING);
    assert(diag_state.diag_logging == 0);

    puts("  [PASS] Consumer UI formatters & IRQ-safe UI callbacks verified");
}

int main(void)
{
    puts("Running Car Audio (iAP1/iAP2) Extension Host Contract Tests...");
    test_descriptor_and_iap1_passthrough();
    test_iap2_mode_lifecycle_and_replug();
    test_hid_control_write_tap();
    test_unload_concurrency_safety();
    test_malformed_inputs_on_production_hooks();
    test_consumer_ui_formatters_and_callbacks();
    puts("All Car Audio extension host contracts PASSED cleanly.");
    return 0;
}
