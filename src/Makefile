#!/usr/bin/make -f
# FortyFifthMidi - plugin build
#
# NAME / FILES_DSP / FILES_UI must be set BEFORE including Makefile.plugins.mk:
# the include expands them into its own rules, so anything assigned afterwards is
# invisible to it and the link fails with "undefined reference to createPlugin()".

NAME = FortyFifthMidi

FILES_DSP = \
	FortyFifthPlugin.cpp

FILES_UI = \
	FortyFifthUI.cpp

include ../dpf/Makefile.plugins.mk

# CLAP and VST3 only. No VST2: the SDK licence was revoked, so new development
# must not target it (see docs/spec.md section 2).
TARGETS = clap vst3

all: $(TARGETS)
