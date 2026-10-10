#!/bin/bash
# run_trace_golden.sh — run the refcount tracer (-trace-refcount) over
# tests/golden/28_refcount_trace/*.jeti and diff the output against *.out.
# Usage: ./run_trace_golden.sh
#   JETIC    path to the jetic binary. Auto-detected: prefer an explicit $JETIC,
#           then target/release/jetic, then target/debug/jetic. Auto-detection
#           matters because a stale binary silently produces line-number-only
#           diffs that look like real regressions.
set -u
cd "$(dirname "$0")/../../.."

if [[ -z "${JETIC:-}" ]]; then
    for cand in target/release/jetic target/debug/jetic; do
        if [[ -x "$cand" ]]; then JETIC="$cand"; break; fi
    done
fi
if [[ -z "${JETIC:-}" || ! -x "$JETIC" ]]; then
    echo "error: jetic binary not found (build it, or set JETIC=)" >&2
    exit 2
fi
echo "using jetic: $JETIC"
DIR=tests/golden/28_refcount_trace
PASS=0
FAIL=0

for np in "$DIR"/*.jeti; do
    out="${np%.jeti}.out"
    if [[ ! -f "$out" ]]; then
        echo "SKIP  $np (no $out)"
        continue
    fi
    # trace with colors disabled for deterministic diffing
    tmp=/tmp/refcount_trace.$$.txt
    "$JETIC" -trace-refcount -trace-no-color -trace-max-iters 2 "$np" > "$tmp" 2>&1
    rc=$?
    if [[ $rc -ne 0 ]]; then
        echo "FAIL  $np (tracer exit $rc)"
        sed 's/^/      /' "$tmp"
        rm -f "$tmp"
        FAIL=$((FAIL + 1))
        continue
    fi
    if diff -u "$out" "$tmp" > /tmp/refcount_trace.diff 2>&1; then
        rm -f "$tmp"
        echo "PASS  $np"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $np"
        sed 's/^/      /' /tmp/refcount_trace.diff
        rm -f "$tmp"
        FAIL=$((FAIL + 1))
    fi
done

echo "----"
echo "trace golden: $PASS passed, $FAIL failed"
exit $((FAIL > 0))
