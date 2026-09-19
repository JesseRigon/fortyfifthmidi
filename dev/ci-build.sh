#!/usr/bin/env bash
# Non-interactive build entry point, used for driving `make` from outside the
# container without nested shell quoting.
set -uo pipefail

cd /workspaces/fortyfifthmidi 2>/dev/null || cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "=== pwd: $(pwd) ==="
make -j"$(nproc)" 2>&1 | tail -60
status=${PIPESTATUS[0]}

echo "=== make exit: ${status} ==="
echo "=== bin/ ==="
ls -la bin/ 2>/dev/null || echo "(no bin/)"

exit "${status}"
