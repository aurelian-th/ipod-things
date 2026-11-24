# SPDX-License-Identifier: GPL-3.0-only
ID := iap2
MODULES := main
main_SOURCES := main.c iap2.c protocol.c link.c rx_tap.c tx.c control.c session.c native_tx.c artwork.c

ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk

$(OUT)/$(ID).rvx: $(OUT)/main.veneers.ok

$(OUT)/main.veneers.ok: $(OUT)/main.elf
	python3 tests/check_veneers.py $(CROSS_COMPILE) $<
	@touch $@

.PHONY: check
check:
	python3 tests/run.py $(SDK)
