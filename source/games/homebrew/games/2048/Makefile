# SPDX-License-Identifier: GPL-3.0-only
ID := 2048
NAME := 2048
SOURCES := main.c game.c tiles.c
ICON := icon.png
HEAP_KIB := 256

# Generated beside the sources so the SDK's object rule finds it by name.
tiles.c: tiles.py tiles.bmp
	python3 tiles.py tiles.bmp $@

include $(SDK)/eapp.mk
