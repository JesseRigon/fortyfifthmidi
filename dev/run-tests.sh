#!/usr/bin/env bash
cd /home/jesse/src/fortyfifthmidi || exit 1
rc=0
for t in test-theory test-chordname test-layout test-binding test-progression; do
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
echo
echo "overall: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
