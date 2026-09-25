# ALPHA — changing fast, expect breakage

This is early alpha. Things change quickly and often: controls move, settings
are added and removed, and a build from last week may not behave like today's.
Saved state is not guaranteed to survive between versions.

---

# FortyFifthMidi

A MIDI generator (CLAP + VST3). It emits **MIDI only** — no audio. A downstream
instrument makes the sound.

## Read this before you install it

**This is AI slop.** Nearly every line of this plugin was written by an AI at
my direction. I am not a C++ developer and I did not review most of it
line-by-line.

**I have no musical ability.** That is the entire reason this exists. I wanted
something that would let me write chord progressions without knowing what a
chord progression is. If you already understand music theory you almost
certainly want a better tool, written by someone who does.

**It is a bag of bugs.** Not "might have a few rough edges" — an actual bag of
bugs. Every session of work on it turns up things that were silently wrong:
chords that sounded notes outside the key, a PLAY button that did nothing
because a state key was never registered, whole columns of the UI lighting up
for no reason, settings that reverted the moment you touched a different
control. Those particular ones are fixed. The next ones are not, because I have
not found them yet.

**Use at your own risk.** Specifically:

- It can emit MIDI you did not ask for. Do not put it in front of anything
  expensive or loud without checking what comes out.
- Saved state may not survive. Do not rely on it to hold a progression you care
  about.
- There is no upgrade path, no versioning discipline and no promise that a
  session saved today opens tomorrow.
- Nobody is on the other end of a bug report. I fix things when they annoy me.

If that is fine with you, it is genuinely fun to play with, and the tests that
do exist are real — see `dev/run-tests.sh`. Six suites, and every one of them
was written because something was actually broken.

The name means nothing. See [docs/lore.md](docs/lore.md) for three invented
explanations, none of which are true.

## Building

Clone it, then run the script for your platform. Each one builds the plugin and
copies the result into `dist/<platform>/`, ready to move wherever your host
scans for plugins. Nothing is installed system-wide unless you ask.

```bash
git clone --recursive https://github.com/JesseRigon/fortyfifthmidi.git
cd fortyfifthmidi

bash scripts/build-linux.sh      # -> dist/linux/
bash scripts/build-windows.sh    # -> dist/windows/
bash scripts/build-macos.sh      # -> dist/macos/
```

`--recursive` matters: DPF is a submodule, and without it the build stops
immediately. If you already cloned without it, run
`git submodule update --init --recursive`.

Common flags, where they apply:

| Flag | Effect |
|---|---|
| `--clean` | Wipe build artifacts first |
| `--install` | Also copy into your user plugin folders (Linux, macOS) |
| `--universal` | arm64 + x86_64 in one bundle (macOS only) |

Each script checks for its own toolchain before starting and tells you what to
install if something is missing. Where plugins go on each platform is printed
when the build finishes.

The Windows script works two ways and picks automatically: natively under MSYS2
or Git Bash with MinGW on `PATH`, or cross-compiled from Linux/WSL with
`mingw-w64`. macOS must be built on a Mac — Apple's SDK is not redistributable,
so there is no cross-compile path.

**The macOS script is untested.** Nobody has run it on a Mac. See
[docs/BUILDING.md](docs/BUILDING.md) for what to expect, including the
Gatekeeper complaint an unsigned bundle will provoke.

Before believing a build does what it should:

```bash
bash dev/run-tests.sh            # six suites
```

## Layout

| Path | What |
|---|---|
| `src/CircleTheory.hpp` | Wheel geometry and chord spelling — no plugin types, unit-testable |
| `src/FortyFifthPlugin.cpp` | MIDI generation, glide state machine |
| `src/FortyFifthUI.cpp` | Nested-ring wheel, hit-testing, visual feedback |
| `scripts/build-*.sh` | Per-platform build + export to `dist/` |
| `dev/run-tests.sh` | All six test suites; run this before believing anything |
| `dev/` | Test sources and local development helpers |
| `docs/spec.md` | Full development specification |
| `docs/BUILDING.md` | Toolchain detail, platform notes, verifying a build |
| `docs/lore.md` | Invented backstory for the name |
| `docs/db-plan.md` | Plan for saved progressions; not built yet |

`build/` is the compiler's scratch directory and `bin/` is where the build system
drops its immediate output. Neither is the thing to copy — take `dist/`.

## What it does

Four screens:

- **⚙ Setup** — what each keyboard key does, the sustain pedal's action, the
  merge window, and where saved data will live.
- **Circle** — the wheel. Click a cell to sound a chord, drag to move between
  them.
- **Slide** — the same chords as vertical strips, laid out by scale degree, for
  touch.
- **Progressions** — a step sequencer. One cell per beat, one row per section,
  sections chaining A→B→C→D. Synced to the host transport, so it only runs when
  your DAW is rolling.

Chords are stored as scale **degrees**, not as notes, so changing the key
transposes everything without rewriting it.

## The one rule worth knowing

Chord type, velocity, note length, glide mode and glide time are **not**
automatable plugin parameters. They are live state, read at the moment of a
gesture and baked into concrete MIDI events.

The practical consequence: changing a setting later can never retroactively
alter MIDI you already recorded. In the code this is why
`FortyFifthPlugin::initParameter()` is deliberately empty and everything goes
through `initState()`/`setState()` instead. Add a real `Parameter` only for a
genuine instrument-level value, such as a future preview synth's filter cutoff.

## Platform note

A plugin only loads on the platform it was built for. A Linux build will not load
in a Windows DAW and vice versa, even though the file extensions match. If a host
silently fails to see the plugin, check what you actually built:

```bash
file dist/*/FortyFifthMidi.clap
# ELF 64-bit ... => Linux
# PE32+ ...      => Windows
# Mach-O ...     => macOS
```

## Host notes

- **Reaper, Bitwig, Cubase, FL Studio:** insert directly before an instrument in
  the same track's chain.
- **Ableton Live:** does not support third-party MIDI-effect plugins in-chain.
  Put the generator on its own MIDI track and route a second track's "MIDI From"
  to it. This is an Ableton limitation, not a bug.
- **Logic Pro:** historically poor at capturing MIDI out of third-party plugins.

## Licensing

Plugin code is MIT. DPF is ISC. Of the formats built here, CLAP is MIT and
VST3 is ISC — DPF uses its own "travesty" API definitions rather than
Steinberg's SDK, so there is no Steinberg licensing obligation.

The whole stack is permissive — a closed-source commercial release is fine, and
the only obligation is preserving copyright notices. See [LICENSE](LICENSE) for
the full breakdown of which terms cover what.

The circle of fifths is music theory and is not copyrightable, nor is a
concentric major/minor ring layout. All visual design here is original; do not
copy the artwork, palette or text of any commercial chord wheel.
