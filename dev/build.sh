#!/usr/bin/env bash
set -euo pipefail

# FortyFifthMidi build helper.
#
#   bash dev/build.sh                 build into bin/
#   bash dev/build.sh --install       ... and install for local Linux hosts
#   bash dev/build.sh --install-win   ... and copy to the Windows VST folder
#   bash dev/build.sh --clean         remove build artifacts first
#
# PLATFORM WARNING: this container is Linux, so `make` produces Linux .clap/.vst3
# binaries. Those load in Linux hosts (Reaper/Bitwig/Carla) and will NOT load in a
# Windows DAW. --install-win is therefore only meaningful when this script runs on a
# build that targeted Windows (see docs/BUILDING.md for the MinGW cross-build).

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

WIN_VST3_DIR="${WIN_VST3_DIR:-/mnt/j/My Drive/Life/5 Media/Music/VSTs}"

do_clean=0
do_install=0
do_install_win=0

for arg in "$@"; do
  case "${arg}" in
    --clean)       do_clean=1 ;;
    --install)     do_install=1 ;;
    --install-win) do_install_win=1 ;;
    *) echo "Unknown option: ${arg}" >&2; exit 2 ;;
  esac
done

if [ ! -f dpf/Makefile.base.mk ]; then
  echo "DPF is missing. Run: bash .devcontainer/scripts/20-dpf.sh" >&2
  exit 1
fi

[ "${do_clean}" -eq 1 ] && make clean

make -j"$(nproc)"

echo
echo "Built artifacts:"
find bin -maxdepth 2 \( -name '*.clap' -o -name '*.vst3' \) -print 2>/dev/null \
  || echo "  (nothing in bin/ - did the build fail?)"

# --- local Linux install -----------------------------------------------------
if [ "${do_install}" -eq 1 ]; then
  mkdir -p ~/.clap ~/.vst3
  [ -e bin/FortyFifthMidi.clap ] && cp -r bin/FortyFifthMidi.clap ~/.clap/
  [ -e bin/FortyFifthMidi.vst3 ] && cp -r bin/FortyFifthMidi.vst3 ~/.vst3/
  echo "Installed to ~/.clap and ~/.vst3 (Linux hosts)."
fi

# --- Windows VST folder copy -------------------------------------------------
if [ "${do_install_win}" -eq 1 ]; then
  if [ ! -d "${WIN_VST3_DIR}" ]; then
    echo "WARNING: Windows VST folder not visible at:" >&2
    echo "  ${WIN_VST3_DIR}" >&2
    echo "  (expected when running inside the devcontainer - /mnt/j is a WSL mount," >&2
    echo "   not a container mount. Run this from WSL, or set WIN_VST3_DIR.)" >&2
    exit 1
  fi

  if file bin/FortyFifthMidi.vst3/Contents/*/*.so >/dev/null 2>&1; then
    echo "WARNING: bin/ holds LINUX binaries; a Windows DAW cannot load them." >&2
    echo "         See docs/BUILDING.md for the MinGW cross-build." >&2
  fi

  cp -r bin/FortyFifthMidi.vst3 "${WIN_VST3_DIR}/" 2>/dev/null \
    && echo "Copied FortyFifthMidi.vst3 -> ${WIN_VST3_DIR}"
  cp -r bin/FortyFifthMidi.clap "${WIN_VST3_DIR}/" 2>/dev/null \
    && echo "Copied FortyFifthMidi.clap -> ${WIN_VST3_DIR}"
fi
