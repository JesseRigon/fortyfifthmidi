#!/usr/bin/env bash
# Build and launch the standalone test rig from WSL, where WSLg supplies a display.
#
# Run this from a host with a display: a headless shell or container has no X
# socket mounted. Build artifacts land in bin/ either way, so building here does
# not disturb anything in the container.
set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_DIR}"

if [ -z "${DISPLAY:-}" ]; then
  echo "DISPLAY is unset - WSLg may not be running." >&2
  echo "Check that 'ls /tmp/.X11-unix' shows X0." >&2
  exit 1
fi

echo "Building standalone (monitor enabled)..."
make -C src monitor -j"$(nproc)" 2>&1 | tail -20
status=${PIPESTATUS[0]}
[ "${status}" -eq 0 ] || { echo "Build failed." >&2; exit "${status}"; }

echo
echo "Launching bin/FortyFifthMidi on DISPLAY=${DISPLAY}"
echo "Click the wheel; the event log is the panel along the bottom."
echo "Close the window to exit."
echo

# JACK is absent here, so DPF falls back to its native/dummy audio driver. That is
# fine: this plugin makes no audio, and the MIDI path does not depend on the
# backend.
exec ./bin/FortyFifthMidi "$@"
