#!/usr/bin/env bash
# Inspect the built artifacts. Confirms the spec section 3 bus configuration:
# note output, and no audio buses.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "=== artifacts ==="
find bin -maxdepth 4 -type f \( -name '*.clap' -o -name '*.so' \) -exec ls -la {} \;

echo
echo "=== architecture ==="
find bin -maxdepth 4 -type f \( -name '*.clap' -o -name '*.so' \) -exec file {} \;

echo
echo "=== exported entry points ==="
for f in $(find bin -maxdepth 4 -type f \( -name '*.clap' -o -name '*.so' \)); do
  echo "-- $f"
  nm -D --defined-only "$f" 2>/dev/null \
    | grep -Ei 'clap_entry|GetPluginFactory|ModuleEntry' \
    | awk '{print "   " $3}'
done

echo
echo "=== embedded identity strings ==="
strings bin/FortyFifthMidi.clap 2>/dev/null \
  | grep -Ei 'note-effect|fortyfifth|com\.jesserigon|MIDI-only' \
  | sort -u | head -10
