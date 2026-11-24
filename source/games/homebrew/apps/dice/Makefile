# SPDX-License-Identifier: GPL-3.0-only
ID := dice
NAME := Dice
SOURCES := main.c dice.c solids.c
ICON := icon.png
HEAP_KIB := 320

# Generated beside the sources so the SDK's object rule finds it by name.
solids.c: solids.py solids.h
	python3 solids.py $@

include $(SDK)/eapp.mk
