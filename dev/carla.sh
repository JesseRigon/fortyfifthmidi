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

# Close Carla so it SAVES.
#
# Stop-Process -Force kills it outright, and Carla writes its engine settings
# and its patchbay connections on a clean exit - so a forced kill silently
# discards the audio device and the cables you just drew, and the next launch
# comes up unconnected again. CloseMainWindow asks it to quit properly; the
# force is only a fallback for a hung process.
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
