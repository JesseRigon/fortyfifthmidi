#!/usr/bin/env bash
# Build the macOS CLAP and VST3, and export them to dist/macos/.
#
#   bash scripts/build-macos.sh              build, export to dist/macos/
#   bash scripts/build-macos.sh --install    ... and copy into ~/Library
#   bash scripts/build-macos.sh --clean      wipe build artifacts first
#   bash scripts/build-macos.sh --universal  build arm64 + x86_64 in one bundle
#
# UNTESTED. Nobody has run this on a Mac. It is DPF's documented macOS path and
# the flags are the ones DPF expects, but treat a first run as debugging rather
# than as building. If it fails, the error is more likely here than in DPF.
#
# Not signed and not notarized, so Gatekeeper will object on first load. See the
# note the script prints at the end.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

DIST="${REPO_DIR}/dist/macos"

do_clean=0
do_install=0
do_universal=0

for arg in "$@"; do
  case "${arg}" in
    --clean)     do_clean=1 ;;
    --install)   do_install=1 ;;
    --universal) do_universal=1 ;;
    -h|--help)   sed -n '2,15p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown option: ${arg}" >&2; exit 2 ;;
  esac
done

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This script only runs on macOS — uname says $(uname -s)." >&2
  echo "macOS plugins cannot be cross-compiled from Linux or Windows without" >&2
  echo "the Apple SDK, which is not redistributable. Build on a Mac." >&2
  exit 1
fi

# --- prerequisites -----------------------------------------------------------
xcode-select -p >/dev/null 2>&1 || {
  echo "Xcode command line tools are not installed." >&2
  echo "  xcode-select --install" >&2
  exit 1
}

if [ ! -f dpf/Makefile.base.mk ]; then
  echo "DPF is missing — the submodule was not checked out." >&2
  echo "  git submodule update --init --recursive" >&2
  exit 1
fi

# --- build -------------------------------------------------------------------
[ "${do_clean}" -eq 1 ] && make clean

MAKE_ARGS=()
if [ "${do_universal}" -eq 1 ]; then
  # DPF reads these straight into CFLAGS/LDFLAGS, so both arches must be named
  # in both — a universal binary needs the link step to be universal too.
  ARCH_FLAGS="-arch arm64 -arch x86_64"
  MAKE_ARGS+=(
    CFLAGS="${ARCH_FLAGS}"
    CXXFLAGS="${ARCH_FLAGS}"
    LDFLAGS="${ARCH_FLAGS}"
  )
  echo "Building universal (arm64 + x86_64)."
else
  echo "Building for this Mac only ($(uname -m)). Use --universal for both arches."
fi

make "${MAKE_ARGS[@]}" -j"$(sysctl -n hw.ncpu)"

# --- export ------------------------------------------------------------------
rm -rf "${DIST}"
mkdir -p "${DIST}"

found=0
for a in FortyFifthMidi.clap FortyFifthMidi.vst3; do
  if [ -e "bin/${a}" ]; then
    cp -R "bin/${a}" "${DIST}/"
    found=1
  fi
done

if [ "${found}" -eq 0 ]; then
  echo "Nothing was built — bin/ holds no .clap or .vst3." >&2
  exit 1
fi

echo
echo "=== exported to dist/macos ==="
find "${DIST}" -maxdepth 1 -mindepth 1 -exec basename {} \;
echo
find "${DIST}" -type f -perm +111 -exec lipo -info {} \; 2>/dev/null || true

# --- optional local install --------------------------------------------------
if [ "${do_install}" -eq 1 ]; then
  mkdir -p ~/Library/Audio/Plug-Ins/CLAP ~/Library/Audio/Plug-Ins/VST3
  if [ -e "${DIST}/FortyFifthMidi.clap" ]; then
    rm -rf ~/Library/Audio/Plug-Ins/CLAP/FortyFifthMidi.clap
    cp -R "${DIST}/FortyFifthMidi.clap" ~/Library/Audio/Plug-Ins/CLAP/
  fi
  if [ -e "${DIST}/FortyFifthMidi.vst3" ]; then
    rm -rf ~/Library/Audio/Plug-Ins/VST3/FortyFifthMidi.vst3
    cp -R "${DIST}/FortyFifthMidi.vst3" ~/Library/Audio/Plug-Ins/VST3/
  fi
  echo
  echo "Installed to ~/Library/Audio/Plug-Ins/{CLAP,VST3}"
fi

cat <<'NOTE'

Copy dist/macos/* into either:
  ~/Library/Audio/Plug-Ins/CLAP  and  ~/Library/Audio/Plug-Ins/VST3   (just you)
  /Library/Audio/Plug-Ins/CLAP   and  /Library/Audio/Plug-Ins/VST3    (everyone)

These bundles are UNSIGNED. macOS will refuse to load them, usually as "damaged
and can't be opened", which is misleading — nothing is damaged, it is only
unsigned. To clear the quarantine flag on your own machine:

  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/FortyFifthMidi.clap
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/FortyFifthMidi.vst3

Only do that for plugins you built yourself or otherwise trust.
NOTE
