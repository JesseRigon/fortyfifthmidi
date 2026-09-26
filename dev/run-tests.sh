#!/usr/bin/env bash
cd /home/jesse/src/fortyfifthmidi || exit 1
rc=0

# Theory and model suites: pure, header-only, no DPF.
for t in test-theory test-chordname test-layout test-binding test-progression test-diatonic test-octave test-keyboard test-highlight test-anchor test-legato test-stuck; do
  printf '%-18s ' "$t"
  if g++ -std=c++17 -I src "dev/$t.cpp" -o "/tmp/$t" 2>/tmp/err.txt; then
    out=$("/tmp/$t" 2>&1 | tail -1)
    echo "$out"
    case "$out" in *FAIL*) rc=1 ;; esac
  else
    echo "BUILD FAILED"
    head -5 /tmp/err.txt
    rc=1
  fi
done

# The real plugin, driven through setState() and run() with DPF's own headers
# and dev/harness.hpp supplying the base-class bodies. Needs the DPF include
# paths and FORTYFIFTH_TESTING, which is what opens the plugin's internals to
# the test host - and is defined nowhere else, so nothing ships with it.
#
# -w because DPF's headers are not warning-clean under this project's settings,
# and a wall of third-party warnings would bury a real one from our own code.
printf '%-18s ' "test-plugin"
if g++ -std=c++17 -w -DFORTYFIFTH_TESTING \
       -I src -I dev -I dpf/distrho -I dpf \
       dev/test-plugin.cpp -o /tmp/test-plugin 2>/tmp/err.txt; then
  out=$(/tmp/test-plugin 2>&1 | tail -1)
  echo "$out"
  case "$out" in *FAIL*) rc=1 ;; esac
else
  echo "BUILD FAILED"
  head -8 /tmp/err.txt
  rc=1
fi

echo
echo "overall: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
