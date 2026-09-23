# Slide Mode — design plan

Status: **proposed, not built.** Review and correct before implementation.

## What is being added

1. A top tab bar: **Circle Mode** | **Slide Mode**.
2. A second screen, Slide Mode, modelled on GarageBand's chord strips.
3. A scale setting — diatonic / major pentatonic / minor pentatonic — which
   changes how many slides exist.
4. A key dropdown on the Slide screen.
5. A keyboard remap: white keys C D E F G A B play degrees I–vii of the
   selected key, replacing the current hand-position map.

---

## 1. Slide layout

Slides run left to right in **scale-degree order**, which is how a chord chart
reads and how GarageBand orders its strips.

### Diatonic — 8 slides

| Slide | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Degree | I | ii | iii | IV | V | vi | vii° | I |
| In C | C | Dm | Em | F | G | Am | B° | C |

Slide 8 is the octave: the same chord as slide 1, one octave up. That is what
makes it 8 rather than 7, and it means the strip spans a full scale.

### Major pentatonic — 6 slides

Drops IV and vii°, the two degrees that carry the semitone tension.

| Slide | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Degree | I | ii | iii | V | vi | I |
| In C | C | Dm | Em | G | Am | C |

### Minor pentatonic — 6 slides

The blues/rock shape, read from the relative minor. In the key of C that is
A minor:

| Slide | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Minor degree | i | ♭III | iv | v | ♭VII | i |
| Major degree | vi | I | ii | iii | V | vi |
| In C | Am | C | Dm | Em | G | Am |

**Resolved while implementing:** this needs no chords from outside the key.
i ♭III iv v ♭VII are vi I ii iii V read six degrees round, so every slide is
diatonic and the wheel's existing wedge already covers it. The worry that it
would need its own Circle Mode handling was unfounded — there is a test
asserting all six slides stay diatonic.

---

## 2. Sections within a slide, and the two modes

Each slide is divided into horizontal sections, like a GarageBand chord strip.
A toggle decides what the sections mean — and, crucially, the **left slider
takes over whatever the sections are not doing**, so both dimensions are
always reachable.

### Mode A — "Sections = octave"

| | |
|---|---|
| Sections (top→bottom) | octave +2, +1, 0, −1 |
| Left slider | chord type (triad, 6, 7, 9, add9, sus2, sus4) |

Same chord on every section of a slide, at different octaves. Hitting the top
of a strip is the high voicing, the bottom is the low one.

### Mode B — "Sections = variation" (GarageBand-like)

| | |
|---|---|
| Section 1 (top) | the plain diatonic chord |
| Sections 2..n | variations: 7th, 9th, sus4, 6th … |
| Left slider | octave |

This is the GarageBand behaviour: the top of each strip is the standard chord
and the rows below are alternatives.

The variation list comes from the existing `Extension` enum, which already has
exactly the right members (None, 6, 7, 9, add9, sus2, sus4) and already knows
how to apply them per chord quality — `extendChord()` gives Em7 rather than E7,
so variations stay diatonic.

---

## 3. What is shared with Circle Mode

Everything below the surface. Slide Mode is a **different control surface over
the same engine**, not a second engine:

- `buildCellChord()` — one definition of what notes a cell produces
- voice leading, and its suspension during plain glide
- glide, latch, sustain pedal, single-note mode
- the monitor and the panic button
- velocity, note length, group/refcount bookkeeping

A slide press sends the same `"gesture"` state message a wheel press does. The
DSP does not need to know which screen the user is looking at — which is what
keeps this from doubling the DSP's complexity.

### Mapping a slide to a cell

Each degree already corresponds to a wheel cell:

| Degree | Ring | Offset from key |
|---|---|---|
| I | key | 0 |
| ii | minor | −1 |
| iii | minor | 0 |
| IV | key | −1 |
| V | key | +1 |
| vi | minor | +1 |
| vii° | dim | 0 |

This is the same table the keyboard map uses, so both can share it — one
definition, tested once.

---

## 4. Keyboard remap

**Replaces** the current hand-position map.

| White key | C | D | E | F | G | A | B |
|---|---|---|---|---|---|---|---|
| Degree | I | ii | iii | IV | V | vi | vii° |

Resolved against the selected key, so in G, pressing C plays G major. The
black keys become free again (currently they carry ii/iii/vi/vii°).

This is strictly better than what is there now: it is the layout every chord
chart uses, it frees five keys, and it matches the slide order exactly, so the
two input methods agree.

**Note:** this changes existing behaviour. Anything recorded against the old
map would play different chords. Nothing is released yet, so the plan is to
replace rather than keep both.

---

## 5. UI layout

```
+--------------------------------------------------+
|  [ Circle Mode ]  [ Slide Mode ]                 |  tab bar (new)
+--------------------------------------------------+
|  Latch  Glide  Key  Chords  Lead                 |  existing controls
|  Maj ext/voicing   Min ...   Dim ...             |
+--------------------------------------------------+
| O |                                              |
| C |                                              |
| T |         wheel  OR  slide strips              |
| A |                                              |
| V |                                              |
| E |                                              |
+--------------------------------------------------+
|  MIDI Monitor  (click to expand)          Panic  |
+--------------------------------------------------+
```

The tab bar costs ~28px at the top; everything below shifts down. The left
slider, the control row and the monitor are shared by both screens — only the
central area swaps.

Slide Mode adds, in its own control row: a **key dropdown** and a **scale**
selector (diatonic / maj pent / min pent).

---

## 6. Build order

Each step ends buildable and testable, and each is its own commit.

1. **Degree table in `CircleTheory.hpp`** — one definition mapping degree →
   (ring, offset), plus the scale enum and per-scale degree lists. Tested
   against `degreeInKey()` in all 12 keys, the same way the keyboard map is.
2. **Keyboard remap** — point the existing map at that table. Small, and it
   makes the new mapping playable before any UI exists.
3. **Tab bar + screen switch** — Circle Mode keeps working; Slide Mode renders
   an empty panel. Pure layout, easy to verify.
4. **Slide rendering and hit-testing** — strips, sections, labels.
5. **The two section modes + slider takeover.**
6. **Scale selector and pentatonic slide counts.**
7. **Key dropdown.**

---

## 7. Things I am unsure about

Flagged rather than guessed:

1. **Section count in octave mode.** Four (+2/+1/0/−1) is a guess. Three or
   five are equally defensible.
2. **Which variations, and in what order,** for mode B. The plan uses
   7th → 9th → sus4 → 6th after the plain triad; GarageBand's own order
   differs per strip.
3. **Minor pentatonic and Circle Mode.** Currently planned as Slide-only.
4. **Does the slide strip glide?** Dragging across strips could glide the way
   dragging across wheel cells does. Not planned for the first cut.
