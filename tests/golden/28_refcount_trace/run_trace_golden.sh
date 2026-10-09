#!/bin/bash
# run_trace_golden.sh — run the refcount tracer (-trace-refcount) over
# tests/golden/28_refcount_trace/*.ov and diff the output against *.out.
# Usage: ./run_trace_golden.sh
#   OVELC    path to the ovelc binary. Auto-detected: prefer an explicit $OVELC,
#           then target/release/ovelc, then target/debug/ovelc. Auto-detection
#           matters because a stale binary silently produces line-number-only
#           diffs that look like real regressions.
set -u
cd "$(dirname "$0")/../../.."

if [[ -z "${OVELC:-}" ]]; then
    for cand in target/release/ovelc target/debug/ovelc; do
        if [[ -x "$cand" ]]; then OVELC="$cand"; break; fi
    done
fi
if [[ -z "${OVELC:-}" || ! -x "$OVELC" ]]; then
    echo "error: ovelc binary not found (build it, or set OVELC=)" >&2
    exit 2
fi
echo "using ovelc: $OVELC"
DIR=tests/golden/28_refcount_trace
PASS=0
FAIL=0

for np in "$DIR"/*.ov; do
    out="${np%.ov}.out"
    if [[ ! -f "$out" ]]; then
        echo "SKIP  $np (no $out)"
        continue
    fi
    # trace with colors disabled for deterministic diffing
    tmp=/tmp/refcount_trace.$$.txt
    "$OVELC" -trace-refcount -trace-no-color -trace-max-iters 2 "$np" > "$tmp" 2>&1
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
