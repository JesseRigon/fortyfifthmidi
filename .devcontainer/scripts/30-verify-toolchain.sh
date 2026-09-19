#!/usr/bin/env bash
set -euo pipefail

# Fail loudly at create time rather than halfway through a first build. Each of these
# is something the Dockerfile installs; a miss means the image drifted, not that the
# plugin code is wrong.

missing=0

need_cmd() {
  if command -v "$1" >/dev/null 2>&1; then
    printf '  %-12s %s\n' "$1" "$($1 --version 2>&1 | head -1)"
  else
    printf '  %-12s MISSING\n' "$1"
    missing=1
  fi
}

need_pkg() {
  if pkg-config --exists "$1"; then
    printf '  %-12s %s\n' "$1" "$(pkg-config --modversion "$1")"
  else
    printf '  %-12s MISSING (dev headers)\n' "$1"
    missing=1
  fi
}

echo "Toolchain:"
need_cmd g++
need_cmd make
need_cmd cmake
need_cmd pkg-config
need_cmd git

echo "Headers:"
need_pkg gl
need_pkg x11
need_pkg alsa

if [ "${missing}" -ne 0 ]; then
  echo "Toolchain verification failed - rebuild the container image." >&2
  exit 1
fi

echo "Toolchain verification passed."
