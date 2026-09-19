#!/usr/bin/env bash
set -euo pipefail

# DPF (DISTRHO Plugin Framework, ISC licensed) is vendored as a git submodule so the
# plugin builds from a pinned revision rather than whatever HEAD happens to be. This
# script only *populates* it; the pin itself lives in .gitmodules + the gitlink.
#
# DPF bundles the CLAP and VST3 headers it needs, so there is no separate Steinberg
# SDK download step: `make` produces bin/*.clap and bin/*.vst3 from this checkout
# alone. Idempotent - safe to re-run on every rebuild.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DPF_DIR="${WORKSPACE_DIR}/dpf"
DPF_URL="${DPF_URL:-https://github.com/DISTRHO/DPF.git}"
DPF_REF="${DPF_REF:-develop}"

cd "${WORKSPACE_DIR}"

# Normal path: the submodule is registered, just needs checking out.
if [ -f .gitmodules ] && grep -q 'path *= *dpf' .gitmodules 2>/dev/null; then
  if [ ! -f "${DPF_DIR}/Makefile.base.mk" ]; then
    echo "Populating DPF submodule..."
    git submodule update --init --recursive dpf \
      || echo "WARNING: submodule update failed; falling back to a direct clone." >&2
  fi
fi

# Fallback: no submodule registered (fresh repo, or the update above failed).
if [ ! -f "${DPF_DIR}/Makefile.base.mk" ]; then
  if [ -d "${DPF_DIR}/.git" ]; then
    echo "WARNING: ${DPF_DIR} exists but looks incomplete; leaving it alone." >&2
  else
    echo "Cloning DPF (${DPF_REF}) into ${DPF_DIR}..."
    GIT_TERMINAL_PROMPT=0 git clone --recursive --branch "${DPF_REF}" --depth 1 \
      "${DPF_URL}" "${DPF_DIR}" \
      || { echo "ERROR: could not fetch DPF; run 'make dpf' by hand." >&2; exit 1; }
  fi
fi

if [ -f "${DPF_DIR}/Makefile.base.mk" ]; then
  echo "DPF ready at ${DPF_DIR} ($(git -C "${DPF_DIR}" rev-parse --short HEAD 2>/dev/null || echo 'no rev'))."
else
  echo "DPF is still missing from ${DPF_DIR}." >&2
  exit 1
fi
