# Chord building — one request, four sources

Status: **partly done.** Steps 0-5 landed in e852b94; steps 6-7 (Slide Mode and the sequencer) remain.

Reported: *"in the UI per-cell selections for how to build the chord are
followed, but in keyboard mode it seems that it adheres to the old ring system.
If I change one chord type then they all change in the same level ring. This
shouldn't happen. There shouldn't be two systems for this. The chord creation via
the UI is working perfectly... the keyboard system needs to be made to match.
Also for the slide mode same thing. It's just a different UI to do the same
thing: send a MIDI signal and additional metadata."*

That reading is exactly right, including which side is correct. What follows is
the mechanism, because it is worse than two systems — it is one system that the
UI *overwrites on its way past*.

---

## 1. What is actually happening

### 1.1 The DSP has no per-cell chord settings

```cpp
Extension fRingExtension[kRingCount] = { kExtNone, kExtNone, kExtNone };
Voicing   fRingVoicing[kRingCount]   = { kVoicingRegular, ... };
```

Three extensions and three voicings — **one per ring**, not per cell. Every
sounding chord resolves its type through:

```cpp
ChordType chordTypeForRing(Ring ring, int position) const
{
    return extendChordOrTriad(defaultChordForRing(ring),
                              fRingExtension[ring],       // <- ring-wide
                              cellIsDominant(position, ring, key),
                              semitoneForCell(position, ring, key));
}
```

### 1.2 The UI keeps the real per-cell data, and pushes it just in time

The editor *does* hold per-cell state — `fCellExt[ring][24]`,
`fCellVoicing[ring][24]` — and immediately before each pointer gesture it
**writes the ring-wide DSP setting** to whatever that one cell needs:

```cpp
/* The DSP holds one extension per ring, not per cell, and does not need to
 * know cells can differ: the editor asserts the right value immediately
 * before the gesture that reads it. */
void pushCellExtension(int position, Ring ring)
```

It has exactly two callers, both mouse paths. So the UI is not reading a
per-cell system; it is **mutating a ring-wide one in flight** and relying on
being the only writer.

### 1.3 Why the keyboard is wrong, precisely

A MIDI note never passes through `pushCellExtension()`. `noteOnCell()` calls
`chordTypeForRing()` directly, which reads `fRingExtension[ring]` — **whatever
value the last mouse click happened to leave there**.

So the keyboard does not use "the old ring system" as a separate design. It uses
the *same* storage, seeing stale residue from the UI's last push. Two
consequences, both reported:

- setting one cell's chord type appears to change every cell in that ring,
  because the ring value is the only thing that exists; and
- a key plays the type of whichever cell was last clicked, not its own.

### 1.4 Slide Mode pushes nothing at all

`onSlidePress()` sends `press:pos:ring:octShift` and never pushes an extension or
voicing, so every slide inherits the residue too. Same bug, same cause.

### 1.5 There are in fact THREE chord builders, not two

| Path | How the type is decided |
|---|---|
| Pointer (Circle) | UI pushes per-cell values into the ring setting, then `chordTypeForRing()` |
| Keyboard | `chordTypeForRing()` on stale ring residue |
| Slide | `chordTypeForRing()` on stale ring residue |
| Sequencer | `chordTypeFor(ring, ext, degree)` — **takes the extension as an argument** |

The sequencer is the one that already got it right, and for the reason you give:
a ii-V-I wants sevenths on the ii and V and a plain I, so a ring-wide value could
never express it. `ProgCell` already carries `{ degree, ext, octave }` together.

### 1.6 Why the glide comment no longer applies

The ring-wide design is justified in the source like this:

> *"Uniformity is what keeps within-ring glide legal: every cell in a ring
> yields the same interval pattern, which is exactly what spec 6.1 requires for
> a single pitch bend to carry all voices."*

That was true when single-bend glide existed. It was removed — MPE gives every
voice its own channel, so a chord may change shape mid-glide, and
`canGlideBetween()` now returns `fGlideMode == kGlideMpe`. The constraint that
justified ring-wide settings is gone; only the storage it forced remains.

---

## 2. What must not be lost

Confirmed working, so any regression here is a failure of the refactor:

1. **Per-cell chord type and voicing in Circle Mode.** The behaviour the user
   calls correct. It must keep working identically — the point is to make the
   other paths match it, not to change it.
2. **Per-cell extension in the sequencer.** `ProgCell::ext`, already per cell.
3. **Velocity from the key, not the setting.** A played note carries the
   performer's intent; the randomise amount still applies as a spread around it.
4. **Single-note mode** bypasses chord generation and sounds the root alone,
   from every path.
5. **Chord validity.** `chordExists()` rejects extensions a degree cannot carry;
   a cell may inherit a setting that is fine elsewhere on the ring and not here.
6. **Octave routing.** Base plus per-screen shift, resolved in one place
   (`clampOctave(fOctave + octShift)`), guarded by `dev/test-octave.cpp`.
7. **Voice leading vs chosen bass.** Taking manual control of voicing per cell
   means leading is off for that cell.
8. **The glide, whole.** Everything in `docs/glide-refactor-plan.md` section 1.

---

## 3. The new structure

### 3.1 One request type, carrying everything a chord needs

```cpp
struct ChordRequest {
    int       rootAbove;   /* interval above the tonic, 0..11 */
    ChordType type;        /* quality + extension, already resolved */
    Ring      ring;        /* which wheel ring - for voicing and display */
    int       octave;      /* absolute */
    Voicing   voicing;     /* inversion and spacing */
    uint8_t   velocity;
};
```

`ChordTarget` (from the glide refactor) is the first four of these — the chord's
address. `ChordRequest` is the address plus how to voice and strike it. The two
should compose rather than duplicate: `ChordRequest` holds a `ChordTarget`.

### 3.2 The source decides; the builder obeys

> Every path resolves its own extension and voicing and hands over a complete
> request. `buildCellChord()` never reads `fRingExtension` again.

That single rule removes the whole class of bug, because there is no longer any
shared mutable state for one path to leave stale for another.

### 3.3 Per-cell storage moves to the DSP

The UI's `fCellExt[ring][24]` / `fCellVoicing[ring][24]` become the DSP's own,
synced by state rather than pushed per gesture:

```cpp
int8_t fCellExt[kRingCount][24];      /* kExtDefault = never set */
int8_t fCellVoicing[kRingCount][24];
```

This is what lets a keyboard note resolve *its own* cell's settings. It needs a
state key that carries the whole table, encoded like `keyMap` and `progression`
already are — `stateChanged()` replay then restores it on UI attach.

### 3.4 The four paths become four thin adapters

| Path | Responsibility |
|---|---|
| Pointer | cell → `ChordRequest`, velocity from the setting |
| Keyboard | note → cell → `ChordRequest`, velocity from the key |
| Slide | slide → degree → cell → `ChordRequest`, octave from the section |
| Sequencer | `ProgCell` → `ChordRequest`, extension from the cell |

Each is a few lines. All the chord knowledge sits behind one builder.

### 3.5 What `ext<N>` / `voice<N>` state becomes

Kept as the *default* for cells never individually set, so old sessions still
load and mean what they meant. `pushCellExtension()` goes away — nothing pushes
per-gesture state any more, which also removes a real thread-safety smell: the
UI thread was writing settings the audio thread read, ordered only by hope.

---

## 4. Steps

- [x] **0. Baseline.** 13 suites green, tree committed, build staged.
- [x] **1. Tests first, and they must fail.** Assert through the harness that a
      keyboard note and a pointer click on the *same cell* emit the *same*
      notes, and that setting one cell's type leaves its neighbours alone. These
      must go red before any fix — this is the bug, so a test that passes now is
      testing the wrong thing.
- [x] **2. Per-cell settings carried as data** composing `ChordTarget`. Thread it through
      `buildCellChord()`/`startGroup()` with no behaviour change.
- [x] **3. Move per-cell storage into the DSP**, with a state key for the whole
      table and round-trip coverage.
- [x] **4. Resolve per cell, not per ring.** `chordTypeForRing()` becomes
      `chordTypeForCell()`, reading `fCellExt[ring][pos]` and falling back to the
      ring default. The step-1 tests go green here.
- [x] **5. Delete `pushCellExtension()`** and the UI's push-before-gesture
      mechanism. The UI becomes a pure source of requests.
- [ ] **6. Slide Mode through the same path**, per-cell settings included.
- [ ] **7. Sequencer onto `ChordRequest`**, so `chordTypeFor()` and
      `chordTypeForRing()` collapse into one function.
- [ ] **8. Full pass.** All suites, fault injection on every new obligation, a
      fresh `--recursive` clone build, Windows build staged, and a listening check
      of each path.

---

## 5. Risks worth naming before starting

**The state table could be large.** 3 rings × 24 cells × 2 axes. It encodes
compactly, but `getState`/`setState` are strings and DPF replays the whole map on
UI attach; `dev/test-plugin.cpp` should assert a full round trip rather than
assume it.

**Old sessions must keep sounding the same.** A saved project has `ext<N>`
per-ring values and no per-cell table. Loading one must apply the ring value as
every cell's default, or existing work changes sound on open. This is the most
likely way to break something the user already has.

**The 24-cell limit is a real limit.** `fCellExt[ring][24]` and `position % 24`
work because no ring exceeds 24 segments; the modulo silently aliases if one ever
does. Worth an assertion rather than a comment.

**Voicing may not be per-cell everywhere yet.** The plan assumes voicing follows
extension, but `applyVoicing()` is called with `fRingVoicing[ring]` inside the
builder — so step 4 must move both, or voicing keeps the old bug while extension
is fixed, which would be a confusing half-state.


---

## 6. What landed, and what did not

**Done (e852b94).** The reported fault is fixed: a cell sounds the same chord
whether it is clicked or played from a key, and one cell's type no longer moves
its neighbours. `CellSettings` lives in the DSP, `chordTypeForCell()` and
`voicingForCell()` replace the ring-wide lookups, the table travels as one state
key, and `pushCellExtension()` is gone.

Rather than a `ChordRequest` struct, voicing joined the existing `ChordTarget` —
the chord's address already carried root, type, ring and octave, and voicing
belongs with them. A separate request type would have duplicated four fields to
add one.

**A second bug the tests found.** The glide's snap rebuilds from the source the
phrase *started* on, so it arranged the landed chord with the **origin** cell's
voicing: a keyboard glide arrived on `[62 65 69]` where a press on the same cell
gives `[65 69 74]`. Same chord, wrong inversion, heard as the glide landing and
then jumping.

**Not done.** Steps 6 and 7. Slide Mode still pushes its own `fSlideRingExt[]`
before each gesture, and the sequencer still has its own `chordTypeFor()`. Both
work today; neither shares the wheel's per-cell table. Slide Mode is the more
interesting of the two, because its own source comment records a past bug where
the two screens' settings leaked into each other — the same class of fault as the
one just fixed, and the reason `fSlideRingExt[]` exists at all.

## 7. Both axes had to move together

Extension changes the *number* of notes; voicing changes *which* notes. So a note
count catches a wrong extension and is blind to a wrong inversion — reverting
voicing to a ring-wide lookup passed the entire suite until an assertion compared
pitches. Fixing extension alone would have produced the right chord in the wrong
inversion from a keyboard, which is harder to diagnose than the original bug.

The same blindness hid the snap bug. Both were found by fault injection, not by
reading the code.
