# Circle of Fifths MIDI Generator Plugin — Development Specification

## 1. Project Summary

A cross-platform audio plugin (VST3 + CLAP, AU optional) that presents a circular
Circle-of-Fifths interface. Clicking/tapping a position triggers a chord or note
built from that root. Dragging between positions glides the sound smoothly between
roots. The plugin is a **MIDI generator ("MIDI effect")**, not an audio-generating
instrument — it outputs MIDI note/CC/pitch-bend data, which downstream instruments
turn into sound.

**Core design principle:** all time-varying musical decisions (chord shape,
velocity, note length, glide) must be committed as real, concrete MIDI events at
the moment of interaction — not stored as retroactive/automatable plugin
parameters. Once a note is generated, changing the plugin's settings afterward
must never alter previously generated MIDI. Only true instrument-level timbre
parameters (if any are added later) should behave as standard automatable plugin
parameters.

---

## 2. Licensing & Technical Foundation

| Component | Choice | License |
|---|---|---|
| Plugin format(s) | VST3 (SDK 3.8+) and CLAP | MIT (both) |
| Optional format | AU (macOS/iOS) | Apache 2.0 SDK |
| Plugin/host framework | iPlug2 or DPF | MIT / ISC |
| Audio I/O (standalone build) | miniaudio or PortAudio | MIT-0 / MIT |
| MIDI I/O (standalone build) | RtMidi | MIT-style |

**Do not** target VST2 — Steinberg revoked the VST2 SDK license; no new
development should use it. **Do not** use any GPL/AGPL-licensed hosting
framework (JUCE, Tracktion Engine) if a permissively-licensed final product is
required — use iPlug2 or DPF instead, and implement VST3/CLAP hosting/plugin
code directly against the now-MIT VST3 SDK and the MIT CLAP SDK.

Pin VST3 SDK to version **3.8 or later** specifically for the MIT license terms;
earlier versions are GPLv3/proprietary dual-licensed.

### 2.1 Decision record (this implementation)

**DPF was chosen** over iPlug2. ISC is functionally equivalent to MIT: permissive,
no source-disclosure obligation, closed-source commercial release permitted, and
the only requirement is preserving the copyright and permission notice. The
deciding factors were technical rather than legal — DPF's Linux CLAP/VST3 build
is the more reliable of the two, and it bundles the CLAP and VST3 headers, so no
separate Steinberg SDK download step is needed.

---

## 3. Plugin Bus / Category Configuration

This must be configured correctly at the plugin-declaration level so hosts
expose correct routing options automatically:

- Declare the plugin as a **MIDI-generating instrument with an event output
  bus** (VST3: event output bus in addition to/instead of audio bus; CLAP:
  note ports with output capability).
- The plugin should have **no required audio output** — it is MIDI-only. An
  audio output bus is optional and only relevant if you later add audio
  passthrough or a built-in preview synth (not required for v1).
- Category: MIDI Effect / Note Effect, not "Instrument" (Instrument categories
  typically imply audio output in most host UIs).

**Host behavior note (do not implement, just be aware):** once correctly
declared, hosts handle same-track vs. separate-track routing, pre/post
position in the FX chain, and MIDI capture/recording entirely on their own.
The plugin's only job is correct MIDI generation — no DAW-specific code is
needed.

---

## 4. UI: Circle of Fifths Interface

- Circular layout, 12 positions, standard circle-of-fifths ordering (C, G, D,
  A, E, B, F#/Gb, Db, Ab, Eb, Bb, F).
- Support both mouse (click+drag) and touch (press+drag) input.
- Single tap/click on a position: triggers a chord/note from that root
  (per current settings) and holds until release.
- Click-drag / touch-drag from one position to another: see Section 6 (Glide).
- Visual feedback: highlight active root position, show a moving indicator
  during glide drag.

### 4.1 Nested ring layout (this implementation)

The wheel is **concentric**, not a single ring: an outer ring of majors, an
inner ring of their relative minors, and a third innermost ring reserved for
diminished chords.

Legal note: the circle of fifths is music theory — a fact/system, not
copyrightable subject matter (17 U.S.C. §102(b)) — and the concentric
major/minor arrangement is the standard pedagogical representation found in
countless sources predating any commercial product. What *is* protected is the
specific visual expression of a given commercial wheel: its palette, window and
cutout shapes, typography, proportions, decorative artwork and explanatory
text, plus its product name as a trademark. All visual design here is therefore
original, and no artwork or text from any commercial chord wheel may be traced
or reproduced.

### Controls (all editor-only, non-automatable — see Section 5)
- **Mode toggle:** Single Note / Chord.
- **Chord type selector:** major, minor, major7, minor7, dominant7,
  sus2, sus4, dim, aug, etc. (extendable list).
- **Octave/voicing offset.**
- **Velocity control:** fixed value, or velocity curve/range if input velocity
  is available (e.g., from an underlying MIDI controller triggering the
  plugin), plus a "randomize velocity ± N" option.
- **Note length control:** for click/tap-triggered notes not tied to a real
  held gesture (e.g., a fixed default duration mode) vs. hold-to-sustain mode.
- **Glide toggle:** On / Off (see Section 6).
- **Glide time:** ms, used when Glide is On.

---

## 5. Parameter Architecture: Settings vs. MIDI Data

This is the most important architectural rule in the spec — read carefully.

- **None of the musical-decision controls in Section 4 (chord type, velocity,
  note length, glide on/off, glide time) should be implemented as standard
  automatable VST3/CLAP parameters that get recorded as automation.**
- Instead, they are **live plugin state** read at the moment of each user
  interaction, used to generate concrete MIDI events, and then discarded from
  the perspective of already-generated notes.
- Practical effect: if the user changes "chord type" from major7 to minor7
  partway through a session, previously recorded MIDI clips are completely
  unaffected — because they contain baked note data, not a reference to a
  "chord type" parameter.
- **Exception:** if a future version adds true instrument-level parameters
  (e.g., a built-in preview synth's filter cutoff), those should use normal
  automatable parameters as usual, since they represent an actual instrument
  characteristic that should behave like every other plugin parameter.

### 5.1 How this is enforced in code

`FortyFifthPlugin::initParameter()` is deliberately empty and the plugin is
constructed with a parameter count of zero. Every setting goes through DPF's
`initState()` / `setState()` / `getState()`, which hosts persist as opaque
state rather than exposing as an automation lane. Settings changed mid-gesture
do not affect the in-flight gesture, which keeps the values it started with.

---

## 6. Glide (Portamento) Behavior

Glide applies to chord-to-chord and note-to-note transitions when the user
drags between two Circle-of-Fifths positions without releasing.

### 6.1 Key simplification
Because the chord *shape* (interval pattern) does not change during a glide —
only the root changes — every voice in the chord moves by the **same number of
semitones**. This means glide can be implemented as a **single uniform pitch
bend applied to all currently-held notes**, not per-voice MPE. MPE is not
required for this feature.

### 6.2 Implementation
1. On initial press: send Note On for all current chord tones (single MIDI
   channel), using velocity/timing per current settings.
2. Before any glide occurs, send an **RPN 0,0 (Pitch Bend Sensitivity)**
   message to set bend range wide enough to cover the largest interval a
   circle-of-fifths drag could require (recommend ±12 semitones to be safe,
   covering multi-step drags around the circle).
3. On drag to a new position: do **not** send new Note On/Off messages yet.
   Instead, ramp Pitch Bend messages from 0 to the value corresponding to the
   semitone distance between old root and new root, over the configured
   Glide Time. Use a smooth interpolation curve (linear is acceptable for v1;
   consider easing curve as a future enhancement).
4. On reaching the target position (or on release): resolve the glide by
   choosing one of:
   - **(a) Snap-and-reset (recommended):** at the moment the bend ramp
     completes, send Note Off for the old pitches and Note On for the true
     target pitches, then reset Pitch Bend to 0. This keeps the recorded MIDI
     note data at correct, editable pitch values — important since downstream
     piano-roll editing should show real notes, not permanently bent ones.
   - **(b) Leave bent:** hold the bend and only resolve on final note-off.
     Simpler, but leaves incorrect note-number data in the MIDI clip (the
     bend does the real pitch work). Not recommended as default; may be
     offered as an advanced/alternate mode later.
5. If Glide is **Off**: standard behavior — release old notes (or not,
   depending on legato settings) and trigger new notes immediately at the new
   root with no pitch bend involved.

### 6.3 Fallback / compatibility note
Some downstream instruments may not respond well to wide pitch bend ranges or
may not honor RPN messages promptly. This should be tested against a handful
of common synths (e.g., a stock DAW synth, Serum, a Native Instruments
instrument) during QA. If inconsistent behavior is found, consider exposing an
alternate "Simple Portamento" mode that instead uses overlapping Note On
messages plus CC65 (portamento on/off) and CC5 (portamento time), relying on
the receiving synth's own portamento engine rather than plugin-generated pitch
bend. This should be a secondary/fallback mode, not the default.

---

## 7. MIDI Event Summary (what the plugin actually emits)

| Event | When |
|---|---|
| Note On (all chord tones or single note) | On initial press of a circle position |
| Note Off | On release, or on glide resolution (Section 6.2 step 4) |
| Pitch Bend | During glide ramp (Section 6.2 step 3), reset to 0 on resolution |
| RPN 0,0 (Bend Sensitivity) | Once, before first glide, or on plugin init |
| Velocity | Encoded directly in each Note On per current velocity settings |
| Note timing/length | Encoded directly via Note On/Off timing — no separate "length parameter" needed in MIDI terms |

No other custom automation lanes or plugin-parameter-driven MIDI encoding
should be used for these musical behaviors.

---

## 8. Host / DAW Compatibility Notes (informational, no plugin-side work required)

These are provided for QA/testing awareness only — the plugin does not need
DAW-specific code for any of this, since correct bus/category declaration
(Section 3) is sufficient for compliant hosts to handle it automatically.

- **Reaper, Bitwig, Cubase, FL Studio, Logic Pro:** support inserting a MIDI
  generator plugin directly before an instrument in the same track's plugin
  chain.
- **Ableton Live:** does **not** support third-party MIDI-effect plugins
  in-chain before an instrument on the same track. Users must place the
  generator on its own MIDI track and route ("MIDI From") a second track's
  input to it, then arm and record. This is a known Ableton limitation, not a
  plugin bug — document it in end-user help material.
- **Logic Pro:** historically has poor/no support for capturing AU MIDI
  output from third-party plugins into another track. If Logic support is a
  priority, plan for VST3 (via a wrapper) or accept this as a known
  limitation; do not spend engineering time trying to work around it at the
  plugin level.

---

## 9. Testing Checklist

- [ ] Verify plugin loads as MIDI-only (no forced audio output requirement) in
      Reaper, Bitwig, Cubase, FL Studio, Ableton, Logic.
- [ ] Verify same-track chaining works in Reaper/Bitwig/Cubase/FL/Logic.
- [ ] Verify separate-track routing workflow works in Ableton.
- [ ] Verify recorded MIDI on a target track is fully independent of later
      plugin setting changes (chord type, velocity, glide) on the generator.
- [ ] Verify glide produces correct, uniformly-transposed chord movement.
- [ ] Verify RPN bend-range message is honored by at least 3 different
      synths; document any that ignore it or behave inconsistently.
- [ ] Verify snap-and-reset glide resolution leaves correct, editable note
      pitches in the recorded MIDI clip (not permanently pitch-bent).
- [ ] Verify velocity and note-length settings are captured correctly into
      generated Note On/Off events across all supported hosts.
- [ ] Cross-platform build verification: Windows, macOS (Intel + Apple
      Silicon), Linux (CLAP/VST3 only — no AU).

---

## 10. Out of Scope for v1 (possible future work)

- MPE support for independently-moving chord voices (not needed given the
  fixed-shape-transposition simplification in Section 6.1).
- Built-in preview synth / audio output.
- Scale-lock / non-diatonic chord substitutions.
- Arpeggiator mode.
- Preset save/load for chord-type/glide configurations (separate from MIDI
  data itself).

---

## 11. Implementation status

Scaffold stage. What exists:

- Toolchain, DPF vendoring, per-platform build scripts in scripts/.
- Correct MIDI-only bus declaration (§3) in `src/DistrhoPluginInfo.h`.
- Parameter architecture (§5) enforced as described in §5.1.
- Theory core (§4.1, §6.1) in `src/CircleTheory.hpp`.
- Nested-ring UI with hit-testing and visual feedback (§4).
- Glide ramp and snap-and-reset resolution (§6.2 steps 2–4a) in the plugin.

Not yet done: wiring UI gestures to the plugin across the DPF UI/DSP boundary
(the UI currently updates its own visual state only), the settings panel for
§4's controls, note-length/hold-to-sustain timing in real host time, velocity
from incoming MIDI, the §6.3 fallback mode, and the entire §9 checklist.
