# FortyFifthMidi

A MIDI-generating Circle of Fifths plugin (CLAP + VST3). Click a position on a
nested wheel to fire a chord; drag between positions to glide between roots.

It emits **MIDI only** — no audio. A downstream instrument makes the sound.

## Quick start

Open the folder in DevPod (from WSL, not from a remote repo URL):

```bash
devpod up ~/src/fortyfifthmidi --ide vscode
```

Then, inside the container:

```bash
make                          # build into bin/
bash dev/build.sh --install   # build and install for local Linux hosts
```

## Layout

| Path | What |
|---|---|
| `.devcontainer/` | Container image, lifecycle scripts, numbered installers |
| `src/CircleTheory.hpp` | Wheel geometry and chord spelling — no plugin types, unit-testable |
| `src/FortyFifthPlugin.cpp` | MIDI generation, glide state machine |
| `src/FortyFifthUI.cpp` | Nested-ring wheel, hit-testing, visual feedback |
| `dev/build.sh` | Build + install helper |
| `docs/spec.md` | Full development specification |
| `docs/BUILDING.md` | Cross-compiling for Windows |

## The one rule worth knowing

Chord type, velocity, note length, glide on/off and glide time are **not**
automatable plugin parameters. They are live state, read at the moment of a
gesture and baked into concrete MIDI events.

The practical consequence: changing a setting later can never retroactively
alter MIDI you already recorded. In the code this is why
`FortyFifthPlugin::initParameter()` is deliberately empty and everything goes
through `initState()`/`setState()` instead. Add a real `Parameter` only for a
genuine instrument-level value, such as a future preview synth's filter cutoff.

## Platform note

The devcontainer is Linux, so `make` produces Linux binaries. They load in Linux
hosts (Reaper, Bitwig, Carla) and **will not** load in a Windows DAW. To test on
Windows, see [docs/BUILDING.md](docs/BUILDING.md).

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
