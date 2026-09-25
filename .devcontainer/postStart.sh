#!/usr/bin/env bash
set -u

# Runs on every container start (not just create). Keep it cheap and idempotent.

WORKSPACE_DIR="$(pwd)"
DPF_DIR="${WORKSPACE_DIR}/dpf"

ensure_workspace_ownership() {
  local owner
  owner="$(id -u):$(id -g)"

  if find "${WORKSPACE_DIR}" -xdev ! -user "$(id -u)" -print -quit | grep -q .; then
    echo "Fixing workspace ownership for ${WORKSPACE_DIR} (${owner})..."
    sudo chown -R "${owner}" "${WORKSPACE_DIR}"
  fi
}

ensure_workspace_ownership

# A missing DPF checkout is the one failure that makes every build fail with a
# confusing error, so check it explicitly and say how to fix it.
if [ ! -f "${DPF_DIR}/Makefile.base.mk" ]; then
  echo "WARNING: DPF is not present at ${DPF_DIR}." >&2
  echo "         Run: bash .devcontainer/scripts/20-dpf.sh" >&2
else
  echo "DPF present ($(git -C "${DPF_DIR}" rev-parse --short HEAD 2>/dev/null || echo 'no rev'))."
fi

cat <<'EOF'

FortyFifthMidi - MIDI-generating Circle of Fifths plugin (CLAP + VST3)

  make            build into bin/
  make clean      remove build artifacts
  bash dev/build.sh --install   build, then copy to ~/.clap and ~/.vst3

  Plugin is MIDI-only: no audio output bus. See README.md and docs/spec.md.
EOF
