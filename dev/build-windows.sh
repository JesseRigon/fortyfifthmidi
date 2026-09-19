#!/usr/bin/env bash
# Cross-compile the Windows CLAP and VST3 with MinGW, then stage them where the
# Windows side can pick them up.
#
# The MIDI monitor is deliberately NOT enabled here: it is a test rig that shares
# a ring buffer between DSP and UI, which is only sound in the single-process
# standalone. The shipping plugin gets none of it.
set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

CROSS=x86_64-w64-mingw32
WIN_STAGE="${WIN_STAGE:-/mnt/j/My Drive/Life/5 Media/Music/VSTs/fortyfifthmidi}"

command -v "${CROSS}-g++" >/dev/null 2>&1 || {
  echo "MinGW not installed. sudo apt-get install mingw-w64" >&2
  exit 1
}

# Windows artifacts must not collide with the Linux ones in bin/.
OUTDIR="${REPO_DIR}/bin-win"
rm -rf "${OUTDIR}" build-win
mkdir -p "${OUTDIR}"

echo "Cross-compiling for Windows (${CROSS})..."

# DGL (the graphics library) is built separately and cached. A Linux libdgl left
# over from the test-rig build will not link against Windows objects, so clean
# BOTH the plugin and DGL before switching toolchains.
make -C src clean >/dev/null 2>&1
make -C dpf/dgl clean >/dev/null 2>&1
rm -rf dpf/build

# Shared by every make invocation below, so DGL and the plugin are built with
# exactly the same toolchain and flags.
CROSS_ARGS=(
  CC="${CROSS}-gcc"
  CXX="${CROSS}-g++"
  AR="${CROSS}-ar"
  WINDRES="${CROSS}-windres"
  CROSS_COMPILING=true
  NOOPT=false
)

# -static links libstdc++/libgcc/winpthread into the plugin, so it loads on a
# machine with no MinGW runtime installed - which is every user's machine.
make -C src "${CROSS_ARGS[@]}" \
  LDFLAGS="-static -static-libgcc -static-libstdc++" \
  -j"$(nproc)" clap vst3 2>&1 | tail -25
status=${PIPESTATUS[0]}

if [ "${status}" -ne 0 ]; then
  echo "Cross-build FAILED." >&2
  exit "${status}"
fi

# DPF writes into bin/; move the Windows results aside so a later Linux build
# cannot overwrite them.
for a in FortyFifthMidi.clap FortyFifthMidi.vst3; do
  [ -e "bin/${a}" ] && cp -r "bin/${a}" "${OUTDIR}/"
done

echo
echo "=== built ==="
find "${OUTDIR}" -maxdepth 3 \( -name '*.clap' -o -name '*.dll' -o -name '*.vst3' \) \
  -exec file {} \;

# Verify these really are Windows binaries before staging them.
if find "${OUTDIR}" -type f \( -name '*.dll' -o -name '*.clap' \) -exec file {} \; \
     | grep -q ELF; then
  echo "ERROR: ELF binaries in the Windows output - the cross-build did not take." >&2
  exit 1
fi

if [ -d "${WIN_STAGE}" ]; then
  echo
  echo "Staging into ${WIN_STAGE}"
  for a in FortyFifthMidi.clap FortyFifthMidi.vst3; do
    if [ -e "${OUTDIR}/${a}" ]; then
      rm -rf "${WIN_STAGE:?}/${a}"
      cp -r "${OUTDIR}/${a}" "${WIN_STAGE}/"
      echo "  copied ${a}"
    fi
  done
else
  echo "Staging dir not found: ${WIN_STAGE}" >&2
  echo "Artifacts are in ${OUTDIR}" >&2
fi

# Clean both again, so the next Linux build does not link Windows objects.
make -C src clean >/dev/null 2>&1
make -C dpf/dgl clean >/dev/null 2>&1
rm -rf dpf/build
echo
echo "Done. Rebuild the Linux test rig with: bash dev/launch.sh"
