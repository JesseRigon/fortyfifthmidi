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

# The MIDI monitor is built into every format. Seeing the exact note, velocity
# and bend stream is as useful inside a DAW as outside one - it is what turns
# "a note is stuck" into "this note-off never went out". The panel is collapsed
# by default, and the UI only drains the event ring while it is open.

BUILD_CXX_FLAGS += -I.

# CLAP and VST3 only. No VST2: the SDK licence was revoked, so new development
# must not target it (see docs/spec.md section 2).
TARGETS = clap vst3

all: $(TARGETS)

# Standalone build, for looking at the UI without a host. Needs a display; under
# WSL, WSLg provides one. Kept as a separate target because `all` deliberately
# ships only the plugin formats.
monitor: jack

.PHONY: all monitor
