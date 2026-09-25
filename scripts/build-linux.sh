#!/usr/bin/env bash
# Build the Linux CLAP and VST3, and export them to dist/linux/.
#
# Everything lands in dist/ so you can copy it wherever your host looks for
# plugins. Nothing is installed system-wide unless you ask with --install.
#
#   bash scripts/build-linux.sh              build, export to dist/linux/
#   bash scripts/build-linux.sh --install    ... and copy to ~/.clap and ~/.vst3
#   bash scripts/build-linux.sh --clean      wipe build artifacts first
#
# NOTE: build/ is DPF's object directory, not an output directory. Exported
# plugins go to dist/, which is why a build never overwrites its own artifacts.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

DIST="${REPO_DIR}/dist/linux"

do_clean=0
do_install=0

for arg in "$@"; do
  case "${arg}" in
    --clean)   do_clean=1 ;;
    --install) do_install=1 ;;
    -h|--help) sed -n '2,12p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown option: ${arg}" >&2; exit 2 ;;
  esac
done

# --- prerequisites -----------------------------------------------------------
missing=()
command -v g++  >/dev/null 2>&1 || missing+=("g++")
command -v make >/dev/null 2>&1 || missing+=("make")
command -v pkg-config >/dev/null 2>&1 || missing+=("pkg-config")

if [ "${#missing[@]}" -gt 0 ]; then
  echo "Missing build tools: ${missing[*]}" >&2
  echo "  Debian/Ubuntu: sudo apt-get install build-essential pkg-config" >&2
  echo "  Fedora:        sudo dnf install gcc-c++ make pkgconf-pkg-config" >&2
  echo "  Arch:          sudo pacman -S base-devel" >&2
  exit 1
fi

# DPF's UI needs X11 and OpenGL headers. Without them the link fails deep inside
# DGL with an unhelpful error, so check up front and say what to install.
for lib in x11 gl; do
  pkg-config --exists "${lib}" || {
    echo "Missing development headers for: ${lib}" >&2
    echo "  Debian/Ubuntu: sudo apt-get install libx11-dev libgl1-mesa-dev \\" >&2
    echo "                                      libxext-dev libxcursor-dev libxrandr-dev" >&2
    echo "  Fedora:        sudo dnf install libX11-devel mesa-libGL-devel \\" >&2
    echo "                                  libXext-devel libXcursor-devel libXrandr-devel" >&2
    exit 1
  }
done

if [ ! -f dpf/Makefile.base.mk ]; then
  echo "DPF is missing — the submodule was not checked out." >&2
  echo "  git submodule update --init --recursive" >&2
  exit 1
fi

# --- build -------------------------------------------------------------------
[ "${do_clean}" -eq 1 ] && make clean

make -j"$(nproc)"

# --- export ------------------------------------------------------------------
# Wiped first: a stale plugin in dist/ that no longer builds is worse than an
# empty dist/, because you cannot tell it is stale by looking at it.
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
echo "=== exported to dist/linux ==="
find "${DIST}" -maxdepth 1 -mindepth 1 -exec basename {} \;

# Confirm these are really ELF. A leftover Windows cross-build in bin/ would
# otherwise be copied out and silently fail to load. Every regular file is
# checked, not just *.so, because the .clap has no telling extension.
if find "${DIST}" -type f -exec file {} \; | grep -q PE32; then
  echo "ERROR: PE binaries in the Linux output — bin/ held a stale cross-build." >&2
  find "${DIST}" -type f -exec file {} \; | grep PE32 >&2
  echo "       Re-run with --clean." >&2
  exit 1
fi

# --- optional local install --------------------------------------------------
if [ "${do_install}" -eq 1 ]; then
  mkdir -p ~/.clap ~/.vst3
  [ -e "${DIST}/FortyFifthMidi.clap" ] && { rm -rf ~/.clap/FortyFifthMidi.clap; cp -r "${DIST}/FortyFifthMidi.clap" ~/.clap/; }
  [ -e "${DIST}/FortyFifthMidi.vst3" ] && { rm -rf ~/.vst3/FortyFifthMidi.vst3; cp -r "${DIST}/FortyFifthMidi.vst3" ~/.vst3/; }
  echo
  echo "Installed to ~/.clap and ~/.vst3"
fi

echo
echo "Copy dist/linux/* into whichever folder your host scans."
