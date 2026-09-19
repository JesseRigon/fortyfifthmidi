/*
 * FortyFifthMidi - Circle of Fifths MIDI generator
 *
 * Plugin-wide compile-time configuration consumed by DPF.
 */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND "FortyFifth"
#define DISTRHO_PLUGIN_NAME  "FortyFifthMidi"
#define DISTRHO_PLUGIN_URI   "https://github.com/JesseRigon/fortyfifthmidi"

#define DISTRHO_PLUGIN_CLAP_ID "com.jesserigon.fortyfifthmidi"

/* --- Bus / category configuration (spec section 3) -------------------------
 *
 * This is a MIDI generator, not an instrument: it emits note data and produces
 * no audio of its own.
 *
 *   IS_SYNTH 0 + NUM_INPUTS/OUTPUTS 0  -> no audio buses at all.
 *   WANT_MIDI_OUTPUT 1                 -> event output bus (VST3) /
 *                                         output note port (CLAP).
 *   WANT_MIDI_INPUT 1                  -> lets an upstream controller trigger
 *                                         positions and supplies input velocity
 *                                         (spec section 4, velocity control).
 *
 * DPF maps these onto each format's category: VST3 kFxTools/"Instrument|Tools"
 * with an event-out bus, and CLAP "note-effect". Hosts then handle same-track
 * chaining and MIDI capture themselves - no DAW-specific code here.
 */
#define DISTRHO_PLUGIN_IS_SYNTH         0
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1

#define DISTRHO_PLUGIN_HAS_UI      1
#define DISTRHO_PLUGIN_WANT_STATE  1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1

#define DISTRHO_PLUGIN_CLAP_FEATURES   "note-effect", "utility"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Tools"

#define DISTRHO_UI_USE_NANOVG   1
#define DISTRHO_UI_DEFAULT_WIDTH  600
#define DISTRHO_UI_DEFAULT_HEIGHT 600

#endif /* DISTRHO_PLUGIN_INFO_H_INCLUDED */
