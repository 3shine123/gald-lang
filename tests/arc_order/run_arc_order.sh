#!/usr/bin/env bash
# tests/arc_order/run_arc_order.sh — ARC evaluation-order regression suite.
#
# WHY THIS EXISTS
# ---------------
# ARC injects `nopa_release` for owned locals at every exit point. **Order
# matters**: the exit expression (`return` / `@throw`) must be FULLY evaluated
# before any local it uses is released — otherwise the release is a
# use-after-free. Conversely a local used only by the exit expression must
# still be released (skipping it is a leak).
#
# Each case is `NN_name.np` + `NN_name.out` (expected stdout under ARC).
# Six combinations are exercised per case:
#   {default, -eh checked, -eh legacy} x {ARC, MRC}
#
#   ARC: stdout must match the .out AND the process must exit 0.
#   MRC: ARC injects nothing (manual memory management), so only exit 0 is
#        asserted — dealloc output is not expected.
#
# Usage: ./run_arc_order.sh            NOPAC=target/debug/nopac ./run_arc_order.sh
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT"

NOPAC="${NOPAC:-}"
if [[ -z "$NOPAC" ]]; then
    for c in target/release/nopac target/debug/nopac; do
        if [[ -x "$c" ]]; then NOPAC="$c"; break; fi
    done
fi
if [[ ! -x "${NOPAC:-}" ]]; then
    echo "error: nopac not found (build it, or set NOPAC=)" >&2
    exit 2
fi
# Absolute paths pass through unchanged: an unconditional "$PWD/$NOPAC"
# would turn an absolute NOPAC into "$PWD/$PWD/target/.../nopac".
case "$NOPAC" in
    /*) ;;
    *) NOPAC="$PWD/$NOPAC" ;;
esac

WORK="${TMPDIR:-/tmp}/nopa_arc_order.$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

MODES=(arc-default arc-checked arc-legacy mrc-default mrc-checked mrc-legacy)

mode_flags() {
    case "$1" in
        arc-default) printf '' ;;
        arc-checked) printf -- '-eh checked' ;;
        arc-legacy)  printf -- '-eh legacy' ;;
        mrc-default) printf -- '-fno-nopa-arc' ;;
        mrc-checked) printf -- '-fno-nopa-arc -eh checked' ;;
        mrc-legacy)  printf -- '-fno-nopa-arc -eh legacy' ;;
    esac
}

norm() {
    awk '{L[n++]=$0} END{while(n>0 && L[n-1]=="") n--; for(i=0;i<n;i++) print L[i]}' "$1"
}

echo "=== ARC evaluation-order suite ==="
echo "nopac: $NOPAC"
echo
printf '%-30s %-12s %s\n' "case" "mode" "result"
printf -- '--------------------------------------------------------------------------\n'

PASS=0; FAIL=0; FAILED=()

for np in "$SCRIPT_DIR"/[0-9][0-9]_*.np; do
    [[ -f "$np" ]] || continue
    name="$(basename "$np" .np)"
    ref="$SCRIPT_DIR/$name.out"
    for m in "${MODES[@]}"; do
        # shellcheck disable=SC2206
        ff=($(mode_flags "$m"))
        # NPLog writes to stderr (runtime.c), so merge the streams: for these
        # cases the program's observable output *is* the stderr transcript.
        log="$WORK/$name.$m.log"
        "$NOPAC" run -o "$WORK/$name.$m.bin" ${ff[@]+"${ff[@]}"} "$np" >"$log" 2>&1
        rc=$?
        ok=1
        note=""
        if [[ $rc -ne 0 ]]; then
            ok=0; note="rc=$rc"
        elif [[ "$m" == arc-* && -f "$ref" ]]; then
            if ! diff -q <(norm "$ref") <(norm "$log") >/dev/null 2>&1; then
                ok=0; note="stdout mismatch"
            fi
        fi
        if [[ $ok -eq 1 ]]; then
            PASS=$((PASS+1)); res=PASS
        else
            FAIL=$((FAIL+1)); FAILED+=("$name/$m"); res="FAIL ($note)"
        fi
        printf '%-30s %-12s %s\n' "$name" "$m" "$res"
    done
done

echo "--------------------------------------------------------------------------"
echo "arc_order: $PASS passed, $FAIL failed"
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo "failing: ${FAILED[*]}"
    echo
    echo "first failing case output ($WORK):"
    for f in "${FAILED[@]}"; do
        n="${f%%/*}"; m="${f##*/}"
        echo "--- $n / $m ---"
        sed 's/^/    /' "$WORK/$n.$m.log" 2>/dev/null | head -14
        echo "    expected:"
        sed 's/^/      /' "$SCRIPT_DIR/$n.out" 2>/dev/null | head -8
        break
    done
fi
exit $((FAIL > 0))
