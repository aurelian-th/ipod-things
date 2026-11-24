# SPDX-License-Identifier: GPL-3.0-only
# Builds the eApp runtime for one app and packs the app into a package folder
# OUT/<ID>/ that goes to /Revola/Games/<ID>/ or /Revola/Apps/<ID>/.
#
#   ID := hello                  # the package id, eapp_application.package_id
#   NAME := Hello                # what Extras lists
#   KIND := game                 # game (under /Revola/Games/) or app (/Revola/Apps/)
#   SOURCES := main.c            # the app's sources
#   ICON := icon.png             # 55 x 55 PNG shown in the list
#   HEAP_KIB := 5120             # the heap the host gives the app
#   FILES := beep.wav            # data files packed into the package by name
#   NAMES := names.toml          # the name per language: de_DE = "Hallo"
#   include $(SDK)/eapp.mk
#
# An app with its own link step (newlib, extra libraries) uses the variables
# instead of the default rules: EAPP_ARCH_FLAGS, EAPP_CFLAGS, EAPP_LDFLAGS,
# EAPP_STARTUP and EAPP_RUNTIME, the targets $(EAPP_STARTUP) and
# $(EAPP_RUNTIME), and `python3 $(SDK)/eapp-pack`.

SDK ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CROSS_COMPILE ?= arm-elf-eabi-
OUT ?= build
KIND ?= game
HEAP_KIB ?= 5120
# The symbols header copied from the reversing repository: the host module ids,
# types and image limits.
EAPP_SYMBOLS := $(SDK)/firmware/osos-2.0.5.h

ifeq ($(filter game app,$(KIND)),)
$(error KIND must be game or app)
endif
$(if $(ID),,$(error ID names the package folder))
$(if $(NAME),,$(error NAME is what Extras lists))
$(if $(ICON),,$(error ICON names a 55 x 55 PNG))

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
EAPP_ARCH_FLAGS := -mcpu=arm926ej-s -marm -mfloat-abi=soft
# Apple's code is 256 MiB away from the image, out of reach of a plain BL.
EAPP_CFLAGS := -std=c11 -Os $(EAPP_ARCH_FLAGS) -mlong-calls \
    -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -I$(SDK) -I$(SDK)/include -I$(SDK)/firmware $(EXTRA_CFLAGS)
EAPP_LDFLAGS := -nostdlib -Wl,-T,$(SDK)/runtime/eapp.lds -Wl,--gc-sections
EAPP_RUNTIME_DIR := $(OUT)/runtime
EAPP_RUNTIME := $(EAPP_RUNTIME_DIR)/libeapp.a
# What every image begins with: the eApp header and the entry code, linked
# as objects so the archive's pull rules do not decide about them.
EAPP_STARTUP := $(EAPP_RUNTIME_DIR)/header.o $(EAPP_RUNTIME_DIR)/startup.o
EAPP_RUNTIME_OBJECTS := $(addprefix $(EAPP_RUNTIME_DIR)/,\
    runtime.o imports.o request.o memory.o time.o input.o files.o directory.o save.o display.o \
    draw.o ui.o wheel.o random.o sound.o music.o system.o gl.o text.o users.o media.o media_image.o)

$(EAPP_RUNTIME): $(EAPP_RUNTIME_OBJECTS)
	$(RM) $@
	$(AR) rcs $@ $^

$(EAPP_RUNTIME_DIR)/%.o: $(SDK)/runtime/%.c $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(EAPP_CFLAGS) -MMD -MP -c $< -o $@

# The interface ids and counts the import list names, in a form the assembler reads.
$(EAPP_RUNTIME_DIR)/imports_ids.h: $(EAPP_SYMBOLS) $(SDK)/eapp.mk
	@mkdir -p $(@D)
	grep -E '^#define (GameAPI_[A-Za-z]+_(Id[0-3]|Count)|GameAPIModule_NameBytes|GameImport_Tail) ' $< > $@

$(EAPP_RUNTIME_DIR)/imports.o: $(SDK)/runtime/imports.S $(SDK)/runtime/imports.def \
		$(EAPP_RUNTIME_DIR)/imports_ids.h $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(EAPP_ARCH_FLAGS) -I$(@D) -I$(SDK)/runtime -c $< -o $@

$(EAPP_RUNTIME_DIR)/startup.o: $(SDK)/runtime/startup.S $(SDK)/eapp.mk
	@mkdir -p $(@D)
	$(CC) $(EAPP_ARCH_FLAGS) -c $< -o $@

-include $(EAPP_RUNTIME_OBJECTS:.o=.d) $(EAPP_RUNTIME_DIR)/header.d

# The default rules for an app of plain C sources without a C library.
EAPP_OBJECTS := $(patsubst %,$(OUT)/app/%.o,$(SOURCES))

$(OUT)/app/%.o: %
	@mkdir -p $(dir $@)
	$(CC) $(EAPP_CFLAGS) -MMD -MP -c $< -o $@
-include $(EAPP_OBJECTS:.o=.d)

$(OUT)/$(ID).elf: $(EAPP_STARTUP) $(EAPP_OBJECTS) $(EAPP_RUNTIME) $(SDK)/runtime/eapp.lds
	$(CC) $(EAPP_ARCH_FLAGS) $(EAPP_STARTUP) $(EAPP_OBJECTS) $(EAPP_RUNTIME) -lgcc \
	    $(EAPP_LDFLAGS) -Wl,-Map,$(@:.elf=.map) -o $@

$(OUT)/$(ID).eapp: $(OUT)/$(ID).elf
	$(OBJCOPY) -O binary $< $@

all: $(OUT)/$(ID)/Manifest.plist

$(OUT)/$(ID)/Manifest.plist: $(OUT)/$(ID).eapp $(ICON) $(FILES) $(NAMES)
	python3 $(SDK)/eapp-pack --kind $(KIND) --id $(ID) --name "$(NAME)" \
	    $(if $(NAMES),--names $(NAMES)) \
	    --image $< --icon $(ICON) --symbols $(EAPP_SYMBOLS) \
	    --heap-kib $(HEAP_KIB) --out $(OUT) $(FILES)

clean:
	rm -rf $(OUT)

.PHONY: all clean
.DEFAULT_GOAL := all
