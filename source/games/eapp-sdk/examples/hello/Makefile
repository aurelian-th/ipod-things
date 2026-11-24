# SPDX-License-Identifier: GPL-3.0-only
ID := hello
NAME := Hello
KIND ?= game
SOURCES := main.c
ICON := icon.png
FILES := beep.wav
NAMES := names.toml
SDK ?= ../..
include $(SDK)/eapp.mk

beep.wav: tone.py
	python3 $< $@

clean: clean-files
clean-files:
	rm -f $(FILES)
.PHONY: clean-files
