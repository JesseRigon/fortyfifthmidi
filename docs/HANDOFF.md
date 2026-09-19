# Handoff — scaffold stage

Written 2026-09-18. Read this first if you are picking the project up inside the
container.

## State: never compiled

**No code in this repo has ever been built.** The scaffold was written on the
Windows side, and the WSL distro has no toolchain (`gcc`, `make`, `cmake` all
absent — they are installed in the *container*, not in WSL). The build was about
to be verified in Docker when work stopped.

Treat every source file as unverified. Expect compile errors on the first
`make`, and expect them in the places called out under "Known risks" below.

## What exists

```
~/src/fortyfifthmidi/
├── .devcontainer/
│   ├── Dockerfile              Ubuntu 24.04 + build tools + X11/GL/ALSA headers
│   ├── devcontainer.json       DPF-oriented; no dotnet/node features
│   ├── post-create.sh          ownership fix, runs scripts/*.sh in sorted order
│   ├── postStart.sh            ownership fix, DPF presence check, usage banner
│   └── scripts/
│       ├── 20-dpf.sh           populates/clones DPF; idempotent
│       └── 30-verify-toolchain.sh  fails loudly if the image drifted
├── src/
│   ├── DistrhoPluginInfo.h     bus/category config (spec §3)
│   ├── CircleTheory.hpp        wheel geometry + chord spelling, no plugin types
│   ├── FortyFifthPlugin.cpp    MIDI generation + glide state machine
│   ├── FortyFifthUI.cpp        nested-ring wheel, hit-testing
│   └── Makefile                TARGETS = clap vst3
├── dev/build.sh                build + install helper
├── docs/{spec.md,BUILDING.md,HANDOFF.md}
├── Makefile
├── README.md
└── dpf/                        cloned, NOT a submodule yet (see below)
```

Git: initialized on `main`, origin set to
`https://github.com/JesseRigon/fortyfifthmidi.git`. **No commit has been made yet**
and the remote repo may not exist yet.

## Decisions already made

- **DPF, not iPlug2.** ISC is permissive and equivalent to MIT for our purposes
  (closed-source commercial release fine; only obligation is preserving the
  notice). Chosen on technical grounds: better Linux CLAP/VST3 builds, and it
  bundles CLAP + VST3 headers so there is no separate Steinberg SDK step.
- **Nested concentric rings** (outer majors, inner relative minors, third ring
  reserved for diminished), as on a printed chord wheel. The theory is public
  domain and the layout is unprotectable; only a specific commercial wheel's
  *visual expression* and product name are protected. All artwork here must stay
  original — see spec §4.1.
- **Spelling is "fortyfifth"**, no U. Corrected from the original request.

## Immediate next steps

1. **Build it.** `devpod up ~/src/fortyfifthmidi --ide vscode`, then `make`.
   Fix what breaks; nothing is verified.
2. **Convert `dpf/` to a real submodule.** It is currently a plain clone with its
   own `.git`, so the pin the spec asks for does not exist:
   ```bash
   rm -rf dpf
   git submodule add https://github.com/DISTRHO/DPF.git dpf
   git submodule update --init --recursive
   ```
   `scripts/20-dpf.sh` already handles both the submodule and clone-fallback
   paths, so it needs no change.
3. **Make the first commit** once something compiles.

## Known risks (where the first build will likely fail)

- **`FortyFifthUI.cpp` NanoVG calls** are written from the DPF API as I recalled
  it, not against the checked-out headers. `arc()` winding constants
  (`NanoVG::CW`/`CCW`), `textAlign` flags and `Color` construction are the most
  likely mismatches.
- **`DISTRHO_PLUGIN_VST3_CATEGORIES`** is set to `"Instrument|Tools"`. Spec §3
  wants MIDI-effect categorisation, and the right VST3 string for a MIDI-only
  plugin may differ in this DPF revision. Verify what hosts actually show.
- **Zero audio buses** (`NUM_INPUTS`/`NUM_OUTPUTS` both 0) is the correct intent
  per spec §3, but some DPF paths assume at least one bus. If it misbehaves,
  investigate before adding a dummy bus — a silent audio bus would violate §3.
- **`run()` ignores `frames` for note-length timing.** `fNoteOffCountdown` is
  decremented in frames but set from milliseconds nowhere; hold-to-sustain is the
  only working mode right now.
- **`kStateCount` is used in the constructor** before the enum is declared later
  in the class body. This compiles in-class in C++ but is worth a look if the
  compiler complains.

## Not implemented at all

- **UI ↔ DSP wiring.** The UI updates its own visuals only; clicking the wheel
  sends no MIDI. This is the single biggest gap — it needs
  `setState()`/`sendNote()` across the DPF UI/DSP boundary, and the glide gesture
  must reach `triggerChordInternal`/`advanceGlide`.
- Settings panel for the spec §4 controls (the state keys exist; no UI for them).
- Velocity from incoming MIDI; `--install-win` cross-build (docs/BUILDING.md has
  the MinGW recipe, untried).
- The §6.3 CC65/CC5 portamento fallback mode.
- Every item in spec §9's testing checklist.

## The rule not to break

Chord type, velocity, note length, glide on/off and glide time are **state, not
parameters** (spec §5). `initParameter()` is deliberately empty and the plugin is
constructed with zero parameters. The point is that changing a setting later can
never retroactively alter already-recorded MIDI. Add a real `Parameter` only for
a genuine instrument-level value, such as a future preview synth's cutoff.

## Testing on Windows

The container is Linux, so `make` yields Linux binaries that a Windows DAW
**cannot** load. The user asked for build output in
`J:\My Drive\Life\5 Media\Music\VSTs` for testing — that requires the MinGW
cross-build in docs/BUILDING.md, and the copy must be done from WSL
(`/mnt/j/...`), since the container cannot see `J:`. `dev/build.sh --install-win`
implements this and warns if the binaries are ELF rather than PE.
