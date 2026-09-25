#!/usr/bin/env bash
# Build the standalone test rig (wheel + MIDI event log). Needs a display to run,
# but not to build.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

make -C src monitor 2>&1 | tail -40
status=${PIPESTATUS[0]}

echo "=== make exit: ${status} ==="
ls -la bin/ 2>/dev/null
exit "${status}"
