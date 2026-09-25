#!/usr/bin/env bash
# Build the Windows CLAP and VST3, and export them to dist/windows/.
#
#   bash scripts/build-windows.sh           build, export to dist/windows/
#   bash scripts/build-windows.sh --clean   wipe build artifacts first
#
# Works two ways, chosen automatically:
#
#   * Natively on Windows under MSYS2/MinGW or Git Bash with MinGW on PATH.
#   * Cross-compiled from Linux/WSL with the x86_64-w64-mingw32 toolchain
#     (Debian/Ubuntu: sudo apt-get install mingw-w64).
#
# The standalone MIDI-monitor build is deliberately NOT produced here. It shares
# a ring buffer between DSP and UI, which is only sound in a single process, so
# it belongs to the test rig and not to a shipping plugin.

set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

DIST="${REPO_DIR}/dist/windows"
CROSS=x86_64-w64-mingw32

do_clean=0
for arg in "$@"; do
  case "${arg}" in
    --clean)   do_clean=1 ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown option: ${arg}" >&2; exit 2 ;;
  esac
done

if [ ! -f dpf/Makefile.base.mk ]; then
  echo "DPF is missing — the submodule was not checked out." >&2
  echo "  git submodule update --init --recursive" >&2
  exit 1
fi

# --- pick a toolchain --------------------------------------------------------
# Native MinGW (MSYS2, Git Bash) already targets Windows, so no CROSS_COMPILING
# and no tool prefixes. Cross-building from Linux needs both.
CROSS_ARGS=()
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    command -v g++ >/dev/null 2>&1 || {
      echo "No g++ on PATH. Install the MinGW toolchain:" >&2
      echo "  MSYS2: pacman -S mingw-w64-x86_64-gcc make pkg-config" >&2
      echo "  ...then run this from the 'MSYS2 MINGW64' shell, not 'MSYS'." >&2
      exit 1
    }
    echo "Building natively on Windows ($(g++ -dumpmachine))."
    ;;
  *)
    command -v "${CROSS}-g++" >/dev/null 2>&1 || {
      echo "MinGW cross-compiler not found: ${CROSS}-g++" >&2
      echo "  Debian/Ubuntu: sudo apt-get install mingw-w64" >&2
      echo "  Fedora:        sudo dnf install mingw64-gcc-c++" >&2
      echo "  Arch:          sudo pacman -S mingw-w64-gcc" >&2
      exit 1
    }
    echo "Cross-compiling for Windows (${CROSS})."
    CROSS_ARGS=(
      CC="${CROSS}-gcc"
      CXX="${CROSS}-g++"
      AR="${CROSS}-ar"
      WINDRES="${CROSS}-windres"
      CROSS_COMPILING=true
      NOOPT=false
    )
    ;;
esac

# --- clean across toolchains -------------------------------------------------
# DGL is built separately and cached. Linux objects will not link against Windows
# ones, and the error surfaces deep inside DGL, so clean BOTH the plugin and DGL
# whenever the toolchain might have changed. This is unconditional rather than
# behind --clean, because getting it wrong costs more than a rebuild does.
make -C src clean >/dev/null 2>&1
make -C dpf/dgl clean >/dev/null 2>&1
rm -rf dpf/build
[ "${do_clean}" -eq 1 ] && rm -rf bin build

# --- build -------------------------------------------------------------------
# -static links libstdc++/libgcc/winpthread into the plugin so it loads on a
# machine with no MinGW runtime — which is every user's machine.
make -C src "${CROSS_ARGS[@]}" \
  LDFLAGS="-static -static-libgcc -static-libstdc++" \
  -j"$(nproc 2>/dev/null || echo 4)" clap vst3 2>&1 | tail -25
status=${PIPESTATUS[0]}

if [ "${status}" -ne 0 ]; then
  echo "Windows build FAILED." >&2
  exit "${status}"
fi

# --- export ------------------------------------------------------------------
rm -rf "${DIST}"
mkdir -p "${DIST}"

found=0
for a in FortyFifthMidi.clap FortyFifthMidi.vst3; do
  if [ -e "bin/${a}" ]; then
    cp -r "bin/${a}" "${DIST}/"
    found=1
  fi
done

if [ "${found}" -eq 0 ]; then
  echo "Nothing was built — bin/ holds no .clap or .vst3." >&2
  exit 1
fi

echo
echo "=== exported to dist/windows ==="
find "${DIST}" -maxdepth 3 \( -name '*.clap' -o -name '*.dll' -o -name '*.vst3' \) \
  -exec file {} \;

# Verify these really are Windows binaries. A cross-build that quietly fell back
# to the host compiler produces ELF, loads nowhere, and gives no other clue.
#
# Every regular file is checked, not just *.dll: DPF names the Windows VST3
# payload FortyFifthMidi.vst3 inside the bundle, so an extension filter would
# skip the one binary most likely to be wrong.
if find "${DIST}" -type f -exec file {} \; | grep -q ELF; then
  echo "ERROR: ELF binaries in the Windows output — the cross-build did not take." >&2
  find "${DIST}" -type f -exec file {} \; | grep ELF >&2
  exit 1
fi

# Clean again, so a following Linux build does not try to link Windows objects.
make -C src clean >/dev/null 2>&1
make -C dpf/dgl clean >/dev/null 2>&1
rm -rf dpf/build

echo
echo "Copy dist/windows/FortyFifthMidi.vst3 into:"
echo "  C:\\Program Files\\Common Files\\VST3\\"
echo "and dist/windows/FortyFifthMidi.clap into:"
echo "  C:\\Program Files\\Common Files\\CLAP\\"
echo
echo "Copy the whole .vst3 FOLDER, not just the DLL inside it."
