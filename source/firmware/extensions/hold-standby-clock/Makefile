# SPDX-License-Identifier: GPL-3.0-only
# make SDK=/path/to/revola-sdk
ID := hold-standby-clock
MODULES := main
main_SOURCES := main.c
ifndef SDK
$(error set SDK to an unpacked revola SDK)
endif
include $(SDK)/rv.mk

.PHONY: check
check:
	python3 tests/run.py $(SDK)

