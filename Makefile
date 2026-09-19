#!/usr/bin/make -f
# FortyFifthMidi - top level

include dpf/Makefile.base.mk

all: plugins gen

plugins:
	$(MAKE) all -C src

ifneq ($(CROSS_COMPILING),true)
gen: plugins dpf/utils/lv2_ttl_generator
	@$(CURDIR)/dpf/utils/generate-ttl.sh

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator
else
gen:
endif

clean:
	$(MAKE) clean -C src
	$(MAKE) clean -C dpf/utils/lv2-ttl-generator
	rm -rf bin build

.PHONY: all plugins gen clean
