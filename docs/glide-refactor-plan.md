# Glide — refactor plan

Status: **in progress.** Functionality is complete and confirmed working; this
is a structural change with no intended behavioural change, except where a bug
is named explicitly below.

The rule for this refactor: **every behaviour in section 1 must survive.** Each
was asked for, built, and confirmed by ear. Anything in section 1 that regresses
is a failure of the refactor, not an acceptable trade.

---

## 1. Functionality that must not be lost

### 1.1 The three glide modes

| Mode | Behaviour |
|---|---|
| `kGlideOff` | No ramp. A new chord retriggers immediately. |
| `kGlideSingle` | One channel-wide pitch bend carries the whole chord. |
| `kGlideMpe` | Every voice on its own channel (2–16), each bending its own distance, so a chord can change SHAPE mid-glide. |

MPE is the only mode where `canGlideBetween()` currently returns true. That is
deliberate for shape changes, but it is also why non-MPE overlaps fall through
to a retrigger — see 1.9.

### 1.2 What can start a glide

Four callers, and all four must keep working:

1. **Pointer drag** across cells (`kGestureMove`) — Circle and Slide.
2. **Keyboard press** while a note is held (`noteOnCell`) — legato.
3. **Keyboard release** falling back to a still-held key (`noteOffCell`).
4. **Octave change** while a pointer chord sounds (`retuneToOctave`).

### 1.3 Distance is absolute, not a pitch class

`fGlideTargetSemis = (root - g->root) + (oct - g->octave) * 12`

Both roots are intervals above the same tonic, so their difference IS the
travel. It may exceed six semitones — I to vii° is genuinely eleven, and
bending the short way lands on the wrong chord. `shortestSemitoneDelta()` is
wrong here.

The octave term is required: a drag into Slide Mode's 8th strip must travel the
full twelve semitones rather than bending within the octave it started in.
Guarded by a source check in `dev/test-octave.cpp`.

### 1.4 Snap-and-reset at the end

Land on the bend, retire the bent notes, restate the true ones, zero the bend.
A recorded clip then holds real, editable pitches rather than permanently bent
ones. (spec 6.2 step 4a)

### 1.5 The landed cell survives the snap

The snap rebuilds via `startGroup()`, which derives the cell from the source it
is handed — and that source is still the one the glide STARTED from. So the
landed cell is captured before the rebuild and restored after. Without this the
highlight snaps back to where the phrase began, moments after the glide moved
it.

### 1.6 Highlight follows the sounding chord

`VoiceGroup::cell` is the single fact the lit set derives from, and anything
that changes what a group sounds must update it. The lights and the sound are
not two systems. Guarded by `dev/test-highlight.cpp`.

### 1.7 Last-note-priority handover

Press A, press B → B owns the phrase. Release B while A is held → the phrase
GLIDES back to A, and the highlight follows. Classic monosynth behaviour.
Guarded by `dev/test-legato.cpp`.

### 1.8 Ownership travels with the glide

`g->midiNote = owner` — the key whose release ends the phrase. Also carried
across the snap's rebuild, or the key holding the chord could no longer release
it.

### 1.9 Known bug, to be fixed by this refactor — not preserved

A press that CANNOT glide falls through to `startGroup()`, and the group it
displaced keeps sounding with its owning key already up. Nothing points at it
and no release can reach it, so its notes stay up permanently.

Reported as: rapid two-key alternation ~20 times, after which MIDI continued
with the plugin bypassed; only retriggering the instrument cleared it.
Bypass could not help because nothing here was sustaining the note — the
note-off simply never went out.

Proven against the real refcount rules (`dev/test-stuck.cpp`, first block):

| Mode | After a lost release |
|---|---|
| non-MPE | **3 notes sounding**, 3 refs — audible stuck chord |
| MPE | 0 sounding, **3 refs** — silent, but those pitches can never sound again |

The second row matters as much as the first: `stopGroup()` suppresses the
note-off whenever `fHeld[note] > 1`, so a phantom reference means that pitch's
count can never reach zero again for the rest of the session.

### 1.10 Refused note-off recovery

A note-off the host refuses sets `fStuckNotes`, retried as an all-notes-off on
the next block. `fOutputFull` latches per block, since DPF forbids writing again
after a refusal. Both must survive.

### 1.11 Everything else that touches glide state

- Latch: pressing a latched cell again stops it, cancelling any glide on it.
- Pedal: holds releases as `deferred`; a deferred group outlives its key.
- Panic: full reset — all notes off on every channel, refcounts zeroed,
  glide cancelled, pedal and key stack forgotten.
- Merge window: two chords a few ms apart both sound; the older's source becomes
  `kMergedSource` so a later move will not try to move it again.
- `zeroAllBends()` on every path that cancels a glide mid-flight.

---

## 2. What is wrong with the current structure

Not behaviour — structure. The behaviour is right; it is held together by
convention rather than by the code.

### 2.1 Glide state is ten loose fields

```
fGlideActive  fGlideElapsed  fGlideDuration  fGlideTargetSemis
fGlideTargetRoot  fGlideTargetType  fGlideTargetRing  fGlideTargetOct
fGlideSource  fGlideTimeMs
```

Nine of them are one cohesive fact — "a glide is in flight, from here to
there" — spread across nine independently assignable variables. Any site that
sets eight of nine leaves the tenth stale, and nothing catches it.

`fGlideActive` alone has **17 write sites**.

### 2.2 The start sequence is written three times

The same ten assignments plus the MPE target loop appear at:

- `glideGroupTo()` — the intended single implementation
- the `kGestureMove` branch — inline near-duplicate
- `retuneToOctave()` — inline near-duplicate

Three copies is why the Slide octave bug existed in one and not the others: the
move branch read `g->octave` where the other two were already correct.

### 2.3 `fGlideSource` identifies the wrong thing

It holds the cell the phrase STARTED on, and is compared against `g->source` in
five places to answer "is this the gliding group?". But:

- a keyboard group's `source` never changes as ownership moves, so two groups
  can share one;
- a merge rewrites `source` to `kMergedSource` outright;
- `findGroup(source)` returns the FIRST active match.

So the handle that identifies the gliding group is neither unique nor stable.
This is the root of 1.9 and of the "is it actually gliding?" question — a second
glide can reuse a stale source while `fGlideElapsed` resets.

### 2.4 Ownership and identity are separate rules

A group is released by `findGroupByNote(owner)` but a glide is found by
`findGroup(source)`. Two different identities for one object, maintained
separately. The gap between them is where notes leak.

### 2.5 Concerns are interleaved

`advanceGlide()` currently does ramp maths, MPE vs single dispatch, the snap,
the group rebuild, cell restoration and bend zeroing. Theory, timing, voice
bookkeeping and MIDI emission are all in one function.

---

## 3. The new structure

### 3.1 One value type for the destination

```cpp
struct ChordTarget {
    int       rootAbove;   /* interval above the tonic */
    ChordType type;
    Ring      ring;
    int       octave;      /* absolute */
};
```

Replaces the four `fGlideTarget{Root,Type,Ring,Oct}` fields. Passed whole, so a
caller cannot set three of four. `buildCellChord()` takes one.

### 3.2 One owner for glide state

```cpp
class Glide {
public:
    bool active() const;
    /* Begin, replacing anything in flight. Returns false if nothing moves. */
    bool begin(VoiceGroup* g, const ChordTarget& to, int owner, ...);
    /* Advance by `frames`; reports whether it has landed. */
    Phase advance(uint32_t frames);
    void  cancel();        /* zeroes bends */
    bool  owns(const VoiceGroup* g) const;
private:
    VoiceGroup* fGroup = nullptr;   /* the group itself, not a source */
    ChordTarget fTo;
    int         fSemis = 0;
    uint32_t    fElapsed = 0, fDuration = 0;
};
```

The state becomes private and the 17 write sites become `begin()` / `cancel()`.

### 3.3 Identify the group by POINTER, not by source

`Glide` holds `VoiceGroup*`. This removes 2.3 and 2.4 outright: there is one
identity, it is unique, and a merge rewriting `source` cannot break it.
`owns(g)` replaces all five `fGlideSource == g->source` comparisons.

Safety: `stopGroup()` must call `fGlide.forget(g)` for the group it retires, so
the pointer can never dangle. That is one rule in one place, versus five
comparisons that each had to be remembered.

### 3.4 Ownership is a group invariant

`VoiceGroup` gains a single accessor pair for `midiNote` so that "who owns this"
has one implementation, and `reclaimOrphaned()` — the 1.9 fix — becomes a
property check on the group rather than a special case at a call site:

> a keyboard group whose owning key is not in the held-key stack, and which is
> not deferred, is unreachable **by definition**.

Exact, not heuristic, so it can never cut short a note anyone is holding.

### 3.5 Separated concerns

| Layer | Responsibility | Where |
|---|---|---|
| Theory | intervals, chord building, target maths | `CircleTheory.hpp` (pure, testable) |
| Ramp | elapsed → progress → per-voice bend amounts | `Glide` |
| Voices | group lifetime, ownership, refcounts | `VoiceGroup` + `stopGroup`/`startGroup` |
| Emission | `sendRaw`, refusal handling, `fOutputFull` | unchanged |

`advanceGlide()` splits into `Glide::advance()` (ramp only) and a `land()` step
that does the rebuild — so the snap's group handling is no longer tangled with
the interpolation.

---

## 4. Steps

Each step ends with all suites passing, so a regression is attributable to one
step. Behaviour is verified between steps, not only at the end.

- [x] **0. Baseline.** Record current behaviour: all 12 suites green, working
      tree committed, Windows build staged.
- [x] **1. Land the 1.9 fix and its test first**, before any restructuring, so
      the bug fix is separable from the refactor and provable on its own.
- [x] **2. Introduce `ChordTarget`** and thread it through `buildCellChord()`
      and the four glide callers. Pure mechanical substitution; no logic moves.
      Done in 2d40738. Also extracted semitonesBetween(), sameChord() and
      glideDurationFrames(), each of which had been written out three times.
      test-octave's literal-text guard was rewritten to assert the property
      rather than the spelling, and verified to still catch the original bug
      (3 failures, up from 1 - the first attempt matched the wrong occurrence
      and passed with the bug present).
- [ ] **3. Extract `Glide`** with the state private and `begin()`/`cancel()`/
      `owns()`. All 17 write sites route through it. Still source-identified.
- [ ] **4. Switch `Glide` to hold `VoiceGroup*`**, add `forget()` in
      `stopGroup()`, and delete the five `fGlideSource == g->source` tests.
- [ ] **5. Collapse the three duplicate start sequences** into `Glide::begin()`.
      The move branch and `retuneToOctave()` become calls.
- [ ] **6. Split `advance()` from `land()`** so the ramp and the rebuild are
      separate, and the landed cell is restored by the layer that owns cells.
- [ ] **7. Verify the MIDI-glide question** left open earlier: with one identity
      and one start path, confirm by ear whether keyboard overlap glides or
      retriggers, and fix if it does not.
- [ ] **8. Full pass.** All suites, a fresh `--recursive` clone build, Windows
      build staged, and a by-ear check of every item in section 1.

---

## 5. How each section-1 behaviour is protected

| Behaviour | Guard |
|---|---|
| 1.3 absolute distance + octave term | `dev/test-octave.cpp` (source check) |
| 1.5 landed cell survives snap | `dev/test-highlight.cpp` |
| 1.6 highlight = sounding chord | `dev/test-highlight.cpp` |
| 1.7 last-note priority | `dev/test-legato.cpp` |
| 1.9 no stranded notes or refs | `dev/test-stuck.cpp` |
| 1.1, 1.2, 1.4, 1.8, 1.10, 1.11 | by ear, per step; no automated guard yet |

The bottom row is the honest gap: six behaviours have no automated test, so
steps 3–6 carry real regression risk and each needs a listening check rather
than a green suite alone.
