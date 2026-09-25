#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="${SCRIPT_DIR}/scripts"
WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ensure_workspace_ownership() {
  local owner
  owner="$(id -u):$(id -g)"

  if find "${WORKSPACE_DIR}" -xdev ! -user "$(id -u)" -print -quit | grep -q .; then
    echo "Fixing workspace ownership for ${WORKSPACE_DIR} (${owner})..."
    sudo chown -R "${owner}" "${WORKSPACE_DIR}"
  fi
}

run_scripts() {
  if [ ! -d "${SCRIPTS_DIR}" ]; then
    echo "No scripts directory found at ${SCRIPTS_DIR}. Skipping custom installs."
    return 0
  fi

  mapfile -t scripts < <(find "${SCRIPTS_DIR}" -maxdepth 1 -type f -name '*.sh' | sort)

  if [ "${#scripts[@]}" -eq 0 ]; then
    echo "No shell scripts found in ${SCRIPTS_DIR}."
    return 0
  fi

  for script in "${scripts[@]}"; do
    echo "Running ${script}"
    bash "$script" || echo "Failed to run ${script}; continuing."
  done
}

ensure_workspace_ownership

echo "Running .devcontainer/scripts installers..."
run_scripts

echo "Post-create completed successfully."
