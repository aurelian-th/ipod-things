# SPDX-License-Identifier: GPL-3.0-only
# Builds every package into dist/Games/<id>/ and dist/Apps/<id>/, the folders
# that go to /Revola/Games/<id>/ and /Revola/Apps/<id>/, and runs the host
# tests that exist. DIST moves the output; a release builds outside the tree.
#
#   make SDK=/path/to/eapp-sdk
#   make check SDK=/path/to/eapp-sdk
#   make clean

ifndef SDK
$(error set SDK to an eApp SDK checkout at the commit in eapp-sdk.lock)
endif

GAMES := $(patsubst %/Makefile,%,$(wildcard games/*/Makefile))
APPS := $(patsubst %/Makefile,%,$(wildcard apps/*/Makefile))
TESTED := $(patsubst %/tests,%,$(wildcard games/*/tests apps/*/tests))
DIST ?= $(abspath dist)

all: $(GAMES) $(APPS)

# The parent folder decides the kind. Every package gets its own OUT: the SDK
# keeps an app's objects under $(OUT)/app/ and two packages would collide.
$(GAMES):
	$(MAKE) -C $@ SDK=$(abspath $(SDK)) KIND=game OUT=$(DIST)/.build/$@ -s
	rm -rf $(DIST)/Games/$(notdir $@)
	mkdir -p $(DIST)/Games
	cp -R $(DIST)/.build/$@/$(notdir $@) $(DIST)/Games/

$(APPS):
	$(MAKE) -C $@ SDK=$(abspath $(SDK)) KIND=app OUT=$(DIST)/.build/$@ -s
	rm -rf $(DIST)/Apps/$(notdir $@)
	mkdir -p $(DIST)/Apps
	cp -R $(DIST)/.build/$@/$(notdir $@) $(DIST)/Apps/

check:
	for package in $(TESTED); do $(MAKE) -C $$package check SDK=$(abspath $(SDK)) || exit 1; done

clean:
	rm -rf $(DIST) $(addsuffix /build,$(GAMES) $(APPS))

.PHONY: all check clean $(GAMES) $(APPS)
