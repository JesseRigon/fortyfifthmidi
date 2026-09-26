#!/usr/bin/env bash
#
# Build, stage and open the test rig - the whole loop in one command.
#
#   bash dev/carla.sh            build, stage, open Carla
#   bash dev/carla.sh --no-build just open Carla
#   bash dev/carla.sh --kill     close Carla
#
# WHY THIS IS A WINDOWS HOST AND NOT A LINUX ONE
#
# WSL2 has no sound card and no USB. /dev/snd holds only 'timer', so there is
# nothing to play through and no way to reach a USB keyboard without USB/IP
# passthrough. Both already work natively on the Windows side, where the V49
# and the audio device live - so the test rig runs there and this script drives
# it from here.
#
# The chain is:
#
#     V49  ->  FortyFifthMidi  ->  Helm  ->  speakers
#
# FortyFifthMidi emits MIDI only, so a synth has to make the sound.
#
# Carla loads the plugin fresh each time it opens, so a rebuild needs Carla
# closed - Windows holds the DLL open and staging would fail. This script
# closes it first.

set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

CARLA_EXE='C:\Tools\Carla-2.5.10-win64\Carla\Carla.exe'
PROJECT_WIN='J:\My Drive\Life\5 Media\Music\VSTs\fortyfifthmidi\fortyfifth-test.carxp'
STAGE='/mnt/j/My Drive/Life/5 Media/Music/VSTs/fortyfifthmidi'

# What the generated project points at. Overridable, because none of it is
# portable: a Windows path to the staged plugin, a synth to make the sound, and
# the controller's name exactly as Windows reports it (midiInGetDevCaps).
RIG_PLUGIN="${RIG_PLUGIN:-J:\\My Drive\\Life\\5 Media\\Music\\VSTs\\fortyfifthmidi\\FortyFifthMidi.vst3\\Contents\\x86_64-win\\FortyFifthMidi.vst3}"
RIG_SYNTH="${RIG_SYNTH:-J:\\My Drive\\Life\\5 Media\\Music\\VSTs\\Helm\\helm64.dll}"
RIG_MIDI="${RIG_MIDI:-V49}"

# Close Carla before the build, since Windows holds the plugin DLL open.
#
# CloseMainWindow is tried first and usually does NOT work - Carla ignores it,
# probably because it wants to prompt - so the force is the normal path rather
# than a fallback. That is survivable: engine settings live in the registry
# under HKCU\\Software\\falkTX\\Carla2 and are written as you change them, and
# the patchbay cables live in the project file, which Carla writes on File >
# Save. Neither depends on a clean exit.
#
# What a forced kill DOES lose is anything unsaved since the last File > Save,
# so save the project after rewiring.
close_carla() {
  powershell.exe -NoProfile -Command "
    \$p = Get-Process Carla -ErrorAction SilentlyContinue
    if (\$p) {
      \$p.CloseMainWindow() | Out-Null
      if (-not \$p.WaitForExit(6000)) {
        Write-Host '  (Carla did not close cleanly; forcing)'
        \$p | Stop-Process -Force
      }
    }" >/dev/null 2>&1
  sleep 1
}

# The saved project is gitignored: it names one machine's paths and devices,
# and Carla rewrites it on every save with the synth's whole parameter set.
# Generate it from the template on a fresh checkout, then leave it alone - it
# is the user's working rig from that point on.
PROJECT_LOCAL="${REPO_DIR}/dev/fortyfifth-test.carxp"
TEMPLATE="${REPO_DIR}/dev/fortyfifth-test.carxp.template"

ensure_project() {
  [ -f "${PROJECT_LOCAL}" ] && return 0

  [ -f "${TEMPLATE}" ] || {
    echo "Missing ${TEMPLATE}" >&2
    return 1
  }

  echo "=== first run: generating dev/fortyfifth-test.carxp ==="

  # Substituted with python, NOT sed.
  #
  # These are Windows paths, and sed reads a backslash in the REPLACEMENT as an
  # escape: "\5 Media" in a path becomes a reference to capture group 5 and sed
  # exits with "invalid reference \5 on `s' command's RHS", writing an empty
  # file. Any path with a digit after a backslash hits it - which is most of
  # them on Windows, and was this author's first one.
  PLUGIN="${RIG_PLUGIN}" SYNTH="${RIG_SYNTH}" MIDI="${RIG_MIDI}" \
  python3 - "${TEMPLATE}" "${PROJECT_LOCAL}" <<'PY' || return 1
import os, sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding='utf-8').read()
for key in ('PLUGIN', 'SYNTH', 'MIDI'):
    text = text.replace('@%s@' % key, os.environ[key])
open(dst, 'w', encoding='utf-8').write(text)
PY

  echo "  plugin : ${RIG_PLUGIN}"
  echo "  synth  : ${RIG_SYNTH}"
  echo "  midi   : ${RIG_MIDI}"
  echo "  Override any of these with RIG_PLUGIN / RIG_SYNTH / RIG_MIDI."
  echo
}

do_build=1
case "${1:-}" in
  --no-build) do_build=0 ;;
  --kill)
    close_carla
    echo "Carla closed."
    exit 0
    ;;
  "") ;;
  *) echo "Unknown option: $1" >&2; exit 2 ;;
esac

# --- close Carla before touching the DLL it has open ------------------------
# Windows holds the VST3 open while Carla runs, so staging would fail.
close_carla

if [ "${do_build}" -eq 1 ]; then
  echo "=== tests ==="
  bash dev/run-tests.sh | tail -3
  echo
  echo "=== building for windows ==="
  bash scripts/build-windows.sh 2>&1 | tail -3 || exit 1
  echo
  echo "=== staging ==="
  # Copied from the Windows side: J: is a Drive mount and is not always
  # visible from WSL, so powershell does the copy.
  powershell.exe -NoProfile -Command "
    \$src = '\\\\wsl.localhost\\Ubuntu-24.04\\home\\jesse\\src\\fortyfifthmidi\\dist\\windows'
    \$dst = 'J:\\My Drive\\Life\\5 Media\\Music\\VSTs\\fortyfifthmidi'
    foreach (\$a in @('FortyFifthMidi.clap','FortyFifthMidi.vst3')) {
      \$t = Join-Path \$dst \$a
      if (Test-Path \$t) { Remove-Item -Recurse -Force \$t -ErrorAction SilentlyContinue }
      Copy-Item -Recurse -Force (Join-Path \$src \$a) \$dst -ErrorAction SilentlyContinue
      if (\$?) { Write-Host \"  staged \$a\" } else { Write-Host \"  FAILED \$a\" }
    }" 2>&1 | grep -E 'staged|FAILED'
fi

ensure_project || exit 1

# The project file lives in the repo; copy it out so Carla's recent-files list
# points somewhere stable rather than into a WSL path.
cp dev/fortyfifth-test.carxp "${STAGE}/" 2>/dev/null \
  || powershell.exe -NoProfile -Command "
       Copy-Item '\\\\wsl.localhost\\Ubuntu-24.04\\home\\jesse\\src\\fortyfifthmidi\\dev\\fortyfifth-test.carxp' \
                 'J:\\My Drive\\Life\\5 Media\\Music\\VSTs\\fortyfifthmidi\\' -Force" >/dev/null 2>&1

echo
echo "=== opening carla ==="
powershell.exe -NoProfile -Command \
  "Start-Process -FilePath '${CARLA_EXE}' -ArgumentList '\"${PROJECT_WIN}\"'" \
  >/dev/null 2>&1

cat <<'NOTE'

Carla is opening with FortyFifthMidi -> Helm already loaded.

FIRST RUN ONLY - two connections to make by hand, in the PATCHBAY tab.

Rack mode chains the PLUGINS to each other automatically, and that part works:
FortyFifthMidi feeds Helm because Helm sits below it. What it does NOT do is
connect the rack to the outside world. Nothing is auto-connected - not the
keyboard, not the speakers. Carla only DISCOVERS devices; it opens one when you
connect it, and never before (CarlaEngineRtAudio.cpp, kExternalGraphConnection*).

So there is no MIDI menu to find. With DirectSound the device list lives on the
Patchbay canvas, not in Settings, and the rack appears there as a box named
"Carla" with these ports:

    Carla
      audio-in1  audio-in2     <- inputs to the rack
      audio-out1 audio-out2    -> the LAST plugin's output
      midi-in                  <- feeds the TOP of the rack
      midi-out

Open the Patchbay tab and drag two cables:

    V49          midi-in   ->  Carla:midi-in        (so the keyboard plays)
    Carla:audio-out1/2     ->  your interface       (so you hear it)

Carla saves both into the project when it exits cleanly, as <ExternalPatchbay>,
so this is once - not once per session. Quit with File > Quit rather than
killing it, or the connections are not written.

After that, run this script again to rebuild: it closes Carla, rebuilds,
restages and reopens with the connections restored.
NOTE
