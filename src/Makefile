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

# The MIDI monitor is a test rig, not a feature: it exists so a developer can see
# the exact note/bend stream the plugin emits, which audio alone cannot show. It
# is compiled ONLY into the standalone build - `make monitor` below - so the
# shipping CLAP and VST3 contain none of it.
#
# Keeping it out of the plugin builds matters: spec section 3 requires a MIDI-only
# plugin, and a monitor state key on the shipping artifact would be visible to
# hosts as saved state.

BUILD_CXX_FLAGS += -I.

# CLAP and VST3 only. No VST2: the SDK licence was revoked, so new development
# must not target it (see docs/spec.md section 2).
TARGETS = clap vst3

all: $(TARGETS)

# Standalone test rig with the event log enabled. Needs a display; under WSL,
# WSLg provides one. See docs/TESTING.md.
monitor:
	$(MAKE) jack BUILD_CXX_FLAGS="$(BUILD_CXX_FLAGS) -DFORTYFIFTH_MIDI_MONITOR=1"

.PHONY: all monitor
