#!/usr/bin/env bash
# Launch the standalone test rig so it OUTLIVES the shell that started it.
#
# A plain `cmd &` from a wsl.exe one-shot dies when that session ends, taking the
# window with it. setsid detaches it into its own session so it keeps running
# until you close the window.
#
#   bash dev/launch.sh          start it
#   bash dev/launch.sh --stop   stop it
#   bash dev/launch.sh --status is it up?

set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${REPO_DIR}/bin/FortyFifthMidi"
LOG=/tmp/fortyfifthmidi.log

case "${1:-start}" in
  --stop)
    pkill -f "${BIN}" && echo "Stopped." || echo "Was not running."
    exit 0
    ;;
  --status)
    if pgrep -f "${BIN}" >/dev/null; then
      echo "Running: $(pgrep -f "${BIN}" | tr '\n' ' ')"
    else
      echo "Not running."
    fi
    exit 0
    ;;
esac

[ -n "${DISPLAY:-}" ] || { echo "DISPLAY unset - is WSLg running?" >&2; exit 1; }

# Always rebuild first. Showing a stale binary wastes a review cycle: you end up
# judging code that is no longer on disk.
echo "Building..."
if ! make -C "${REPO_DIR}/src" monitor -j"$(nproc)" > /tmp/ff-build.log 2>&1; then
  echo "Build FAILED:" >&2
  tail -25 /tmp/ff-build.log >&2
  exit 1
fi
echo "Build ok ($(date -r "${BIN}" '+%H:%M:%S'))."

pkill -f "${BIN}" 2>/dev/null && sleep 0.5

setsid "${BIN}" > "${LOG}" 2>&1 < /dev/null &
disown 2>/dev/null

sleep 3
if pgrep -f "${BIN}" >/dev/null; then
  echo "Running (pid $(pgrep -f "${BIN}" | head -1)). Log: ${LOG}"
  echo "Close the window, or: bash dev/launch.sh --stop"
else
  echo "Failed to start. Log follows:" >&2
  cat "${LOG}" >&2
  exit 1
fi
