#!/usr/bin/env bash
# tests/eh_matrix/gen_quality.sh — generated-C quality stats per EH mode.
#
# The `-eh checked` lowering must not cost what it replaced:
#   * a program with NO @try anywhere must receive NO guards at all
#     (the stage-3 effect analysis exists precisely to keep this at zero)
#   * a program WITH @try may pay guards, but not code bloat or compile time
#   * checked must emit zero setjmp/longjmp (it is not allowed to depend on
#     the hosted-only jmp_buf ABI — the bare-metal mandate)
#   * legacy must reproduce the sjlj output, not merely "an equivalent one"
#
# Usage: NOPAC=target/release/nopac ./gen_quality.sh
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT"

NOPAC="${NOPAC:-}"
if [[ -z "$NOPAC" ]]; then
    for cand in target/release/nopac target/debug/nopac; do
        if [[ -x "$cand" ]]; then NOPAC="$cand"; break; fi
    done
fi
[[ -x "$NOPAC" ]] || { echo "error: nopac not found"; exit 2; }

WORK="${TMPDIR:-/tmp}/nopa_eh_quality.$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

MODES=(default checked legacy)
GOLD="tests/golden"

# label|file|extra args
CASES=(
  "no_try|$GOLD/04_arc/retain_release.np|"
  "single_try|$GOLD/15_exceptions/try_catch.np|-fno-nopa-arc"
  "foundation|$GOLD/13_foundation/04_npstring/npstring_test.np|"
  "big_file|examples/01_JSONEditor/json_editor.np|-fno-nopa-arc"
  "c_superset|$GOLD/22_c_superset/c_superset.np|"
  "freestanding|$GOLD/25_freestanding/freestanding.np|-ffreestanding"
  "no_throw_plain|tests/eh_matrix/samples/no_throw_plain.np|"
  "loops_pure|tests/eh_matrix/samples/loops_pure.np|"
)

printf '%-14s %-9s %8s %8s %8s %9s %8s\n' \
    "case" "mode" "lines" "guards" "setjmp" "bytes" "cc(s)"
printf -- '--------------------------------------------------------------------------\n'
for entry in "${CASES[@]}"; do
    IFS='|' read -r label file extra <<< "$entry"
    for m in "${MODES[@]}"; do
        args=()
        case "$m" in
            checked) args=(-eh checked) ;;
            legacy)  args=(-eh legacy) ;;
        esac
        # shellcheck disable=SC2206
        [[ -n "$extra" ]] && args+=($extra)
        out="$WORK/$label.$m.c"
        if ! $NOPAC -rewrite-nopa "${args[@]}" -o "$out" "$file" >/dev/null 2>&1; then
            printf '%-14s %-9s %s\n' "$label" "$m" "TRANSPILE-FAIL"
            continue
        fi
        lines=$(wc -l < "$out" | tr -d ' ')
        guards=$(grep -c "__nopa_eh_flag" "$out" || true)
        setj=$(grep -cE "setjmp|longjmp" "$out" || true)
        bytes=$(wc -c < "$out" | tr -d ' ')
        # clang compile time of the generated C (only when it can compile)
        TIMEFORMAT='%R'
        t=$( { time clang -std=c99 -fblocks -w -I include -c "$out" -o "$out.o" ; } 2>&1 ) || t="n/a"
        printf '%-14s %-9s %8s %8s %8s %9s %8s\n' \
            "$label" "$m" "$lines" "$guards" "$setj" "$bytes" "$t"
    done
    printf -- '--------------------------------------------------------------------------\n'
done

# ── Hard gate: a program that cannot throw must pay nothing ─────────────────
# no_throw_plain has no @try, no @throw, and no Foundation import (therefore no
# inlined throwing chain). The stage-3 effect analysis must keep its checked
# output completely free of EH guards. NOTE: judge by "any occurrence of
# __nopa_eh_flag", not by a literal `if (__nopa_eh_flag` prefix — the codegen
# parenthesizes the condition (`if ((__nopa_eh_flag == 0))`), so a prefix grep
# reports 0 even when guards are present.
plain_c="$WORK/no_throw_plain.checked.c"
if [[ ! -f "$plain_c" ]]; then
    echo "GATE FAIL no_throw_plain did not transpile under -eh checked"
    exit 1
fi
g=$(grep -c "__nopa_eh_flag" "$plain_c" || true)
if [[ "$g" -eq 0 ]]; then
    echo "GATE OK   no_throw_plain / -eh checked : 0 guards (nothing to pay)"
else
    echo "GATE FAIL no_throw_plain / -eh checked : $g guards (expected 0)"
    exit 1
fi

# Same gate for loop conditions. while / do / for-with-cond used to wrap the
# condition in `(flag == 0) && ...` no matter what the body did, so a pure
# `while (i < 5) i++;` paid a global flag read per iteration.
loops_c="$WORK/loops_pure.checked.c"
if [[ ! -f "$loops_c" ]]; then
    echo "GATE FAIL loops_pure did not transpile under -eh checked"
    exit 1
fi
lg=$(grep -c "__nopa_eh_flag" "$loops_c" || true)
if [[ "$lg" -eq 0 ]]; then
    echo "GATE OK   loops_pure / -eh checked : 0 guards (pure loop conditions unguarded)"
else
    echo "GATE FAIL loops_pure / -eh checked : $lg guards (expected 0)"
    exit 1
fi
