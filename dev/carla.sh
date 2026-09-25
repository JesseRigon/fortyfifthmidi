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

do_build=1
case "${1:-}" in
  --no-build) do_build=0 ;;
  --kill)
    powershell.exe -NoProfile -Command \
      "Get-Process Carla -ErrorAction SilentlyContinue | Stop-Process -Force" \
      >/dev/null 2>&1
    echo "Carla closed."
    exit 0
    ;;
  "") ;;
  *) echo "Unknown option: $1" >&2; exit 2 ;;
esac

# --- close Carla before touching the DLL it has open ------------------------
powershell.exe -NoProfile -Command \
  "Get-Process Carla -ErrorAction SilentlyContinue | Stop-Process -Force" \
  >/dev/null 2>&1
sleep 1

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

First run only:
  1. Settings > Configure Carla > Engine
       Audio driver   : DirectSound or WASAPI
  2. The rack's MIDI input takes the V49 automatically. If it does not,
     click the plugin's left-hand MIDI port and pick it.
  3. Double-click a plugin to open its editor.

After that, just run this script again: it closes Carla, rebuilds, restages
and reopens, so a change is audible in one command.
NOTE
