# Handoff — scaffold stage

Written 2026-09-18. Read this first if you are picking the project up inside the
container.

## State: builds clean, does not yet emit MIDI

Updated 2026-09-18 after the first successful build.

The devcontainer is up and `make` succeeds. Both formats build and load-check:

```
bin/FortyFifthMidi.clap                                  clap_entry
bin/FortyFifthMidi.vst3/Contents/x86_64-linux/*.so       GetPluginFactory, ModuleEntry
```

The CLAP reports `note-effect`, which is the spec §3 categorisation. The NanoVG
UI code compiled without changes, contrary to what was expected.

One bug was found and fixed: in `src/Makefile`, `NAME`/`FILES_DSP`/`FILES_UI`
must be assigned **before** `include ../dpf/Makefile.plugins.mk`. Assigned after,
DPF never sees them, skips compiling the sources, and the link fails with
`undefined reference to createPlugin()`.

**What still does not work: clicking the wheel emits no MIDI.** The UI updates
its own visuals only. That is the next task — see "Not implemented" below.

Committed as `36c8324` on `main`. Not pushed: the GitHub remote does not exist yet.

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

## How to build

The workspace already exists. From WSL:

```bash
devpod up ~/src/fortyfifthmidi --ide vscode     # or --ide none
```

To build without an interactive shell (`devpod ssh` was failing at the tunnel
layer, so target the container by id — `docker ps --filter name=fortyfifthmidi`):

```bash
docker exec -u vscode <container-id> bash /workspaces/fortyfifthmidi/dev/ci-build.sh
docker exec -u vscode <container-id> bash /workspaces/fortyfifthmidi/dev/ci-verify.sh
```

**Do not stop, delete or prune any other container.** `fruitful-orchard-wsl` runs
alongside this one and is in active use.

## Immediate next steps

1. **Wire the UI to the DSP.** The whole point of the plugin, and entirely absent.
2. **Convert `dpf/` to a real submodule.** It is currently a plain clone with its
   own `.git`, excluded from the commit via `.git/info/exclude`, so the pin the
   spec asks for does not exist:
   ```bash
   rm -rf dpf
   git submodule add https://github.com/DISTRHO/DPF.git dpf
   git submodule update --init --recursive
   ```
   `scripts/20-dpf.sh` already handles both the submodule and clone-fallback
   paths, so it needs no change. Remove the `/dpf/` line from
   `.git/info/exclude` when doing this.
3. **Create the GitHub remote** and push. Origin is set to
   `https://github.com/JesseRigon/fortyfifthmidi.git` but the repo does not exist.

## Known risks (compiles, but unverified against a host)

Building is not the same as working. None of the following has been checked in a
real DAW, because the container has no host to load into.

- **`DISTRHO_PLUGIN_VST3_CATEGORIES`** is `"Instrument|Tools"`. The CLAP side
  correctly reports `note-effect`, but whether hosts show the VST3 as a MIDI
  effect is unconfirmed. Check what Reaper/Bitwig actually display.
- **Zero audio buses** (`NUM_INPUTS`/`NUM_OUTPUTS` both 0) is the correct intent
  per spec §3 and it builds, but host behaviour is untested. If something
  misbehaves, investigate before adding a dummy bus — a silent audio bus would
  violate §3.
- **`run()` ignores `frames` for note-length timing.** `fNoteOffCountdown` is
  decremented in frames but never set from milliseconds, so the note-length
  setting does nothing; hold-to-sustain is the only mode that could work.
- **Glide is unreachable.** `advanceGlide()` and the snap-and-reset resolution
  are written but nothing ever sets `fGlideActive`, because the UI is not wired.

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
