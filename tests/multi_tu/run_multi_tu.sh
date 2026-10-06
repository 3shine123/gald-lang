#!/bin/bash
# run_multi_tu.sh — cross-TU link consistency suite.
#
# WHY THIS EXISTS
# ---------------
# Nopa's uniform vtable is built per translation unit from the set of instance
# methods that TU happens to see. Two TUs that see DIFFERENT method sets compile
# two different `struct nopa_vtable` layouts, while the linker weak-merges the
# vtable *instances* into a single allocation. Dispatch through the losing
# layout then reads the wrong slot: silent garbage or a segfault at whatever
# offset the miscalculation lands on. Plain C never hits this because every
# struct definition is spelled out in the source; Nopa synthesises the layout,
# so C's type system cannot protect us.
#
# This suite pins down, per language feature, whether it survives a cross-TU
# link. A feature that cannot is recorded here as an explicit, loud failure —
# not as a mystery crash three weeks later.
#
# Each case is a directory containing:
#   *.nh   declarations shared by both TUs (the only channel between them)
#   lib.np the "library" TU: defines the classes, exposes entry points
#   main.np the "client" TU: sees only the .nh, drives the classes
#   expected.txt  the stdout the program must print (optional; absent = no
#                 output assertion, only "it must run and exit 0")
#
# Usage:  ./run_multi_tu.sh            run everything
#         ./run_multi_tu.sh 05_block   run one case (by dir name or number)
#   NOPAC=path/to/nopac ./run_multi_tu.sh
set -u
# Resolve the script location BEFORE any cd: $0 may be a relative path, and
# resolving it after the cd below anchors it to the project root — invoking
# this script from inside tests/multi_tu then found zero cases.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/../.."

if [[ -z "${NOPAC:-}" ]]; then
    for cand in target/debug/nopac target/release/nopac; do
        if [[ -x "$cand" ]]; then NOPAC="$cand"; break; fi
    done
fi
if [[ -z "${NOPAC:-}" || ! -x "$NOPAC" ]]; then
    echo "error: nopac binary not found (build it, or set NOPAC=)" >&2
    exit 2
fi

CASES_ROOT="$SCRIPT_DIR"
NOPA_INC="$PWD/include"
WORK="${TMPDIR:-/tmp}/nopa_multi_tu.$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

FILTER="${1:-}"
PASS=0; FAIL=0; FAILED_CASES=()

# Emitted C needs -I include (nopa/runtime.h) and the case dir (its own .nh).
# Note: Foundation is NOT imported by most cases — a case that only needs a
# base class uses the implicit root, which keeps the method set (and therefore
# the vtable layout) as small as possible. That is deliberate: the fewer
# selectors a case drags in, the more precisely a failure points at the
# feature under test.
INCS=(-I "$NOPA_INC" -I "$NOPA_INC/Foundation")

run_case() {
    local dir="$1" name
    name="$(basename "$dir")"
    local out="$WORK/$name"
    mkdir -p "$out"

    local sources=()
    while IFS= read -r f; do sources+=("$f"); done < <(find "$dir" -name '*.np' | sort)
    if [[ ${#sources[@]} -eq 0 ]]; then
        echo "SKIP  $name (no .np sources)"; return
    fi

    # 1. transpile every TU separately
    # A case may pin a stable cross-TU vtable layout: a SLOTS_MANIFEST marker
    # file in the case dir makes every TU compile with --slots <path>. The
    # manifest lives in the WORK dir (nopac writes the assignment back after
    # each compile; the case dir must stay clean). First TU creates it; the
    # append-only contract means later TUs keep its slot order.
    local slots_args=()
    if [[ -f "$dir/SLOTS_MANIFEST" ]]; then
        slots_args=(--slots "$out/slots.manifest")
    fi

    # NATIVE marker: drive nopac's own multi-input mode (`nopac main.np
    # lib.np -o app`) instead of transpile-each-then-clang. Keeps the
    # compiler's multi-TU feature itself under test, not just the link
    # mechanics it shares with every other case.
    local native=0
    [[ -f "$dir/NATIVE" ]] && native=1

    # Optional EH backend selection for the acceptance matrix:
    #   NOPA_EH_FLAG="-eh checked" ./run_multi_tu.sh
    # Unset (the default) = the shipped default backend, zero behavior change.
    local eh_args=()
    if [[ -n "${NOPA_EH_FLAG:-}" ]]; then
        # shellcheck disable=SC2206
        eh_args=(${NOPA_EH_FLAG})
    fi

    local tsrc objs=() t
    if [[ $native -eq 1 ]]; then
        # NATIVE: one nopac command compiles and links every TU — the same
        # job every other case does with per-TU -rewrite-nopa + clang below.
        # main.np is the main TU (first input); the rest are extras.
        local main_tu="" log="$out/$name.transpile.log"
        local rest=()
        for t in "${sources[@]}"; do
            if [[ -z "$main_tu" && "$(basename "$t")" == "main.np" ]]; then
                main_tu="$t"
            else
                rest+=("$t")
            fi
        done
        if [[ -z "$main_tu" ]]; then
            main_tu="${sources[0]}"
            rest=("${sources[@]:1}")
        fi
        if ! "$NOPAC" "$main_tu" ${rest[@]+"${rest[@]}"} -I "$dir" "${INCS[@]}" \
                ${slots_args[@]+"${slots_args[@]}"} ${eh_args[@]+"${eh_args[@]}"} \
                -o "$out/$name.bin" > "$log" 2>&1; then
            if [[ -f "$dir/EXPECT_FAIL" ]]; then
                echo "PASS  $name (failed as expected at compile/link)"
                PASS=$((PASS+1)); return
            fi
            echo "FAIL  $name (native multi-input compile)"
            sed 's/^/      /' "$log" | head -12
            FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
        fi
    else
        # 1. transpile every TU separately
        # A case may pin a stable cross-TU vtable layout: a SLOTS_MANIFEST marker
        # file in the case dir makes every TU compile with --slots <path>. The
        # manifest lives in the WORK dir (nopac writes the assignment back after
        # each compile; the case dir must stay clean). First TU creates it; the
        # append-only contract means later TUs keep its slot order.
        for t in "${sources[@]}"; do
            tsrc="$out/$(basename "${t%.np}").c"
            # NOTE: macOS bash 3.2 under `set -u` rejects "${empty_arr[@]}" —
            # the conditional expansion keeps empty slots_args legal.
            if ! "$NOPAC" -rewrite-nopa "$t" -o "$tsrc" -I "$dir" "${INCS[@]}" ${slots_args[@]+"${slots_args[@]}"} ${eh_args[@]+"${eh_args[@]}"} > "$out/$name.transpile.log" 2>&1; then
                echo "FAIL  $name (transpile)"
                sed 's/^/      /' "$out/$name.transpile.log" | head -12
                FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
            fi
            objs+=("$tsrc")
        done

        # 2. link them into one program together with the runtime
        if ! clang -std=c99 -fblocks -w "${INCS[@]}" \
                "${objs[@]}" "$NOPA_INC/nopa/runtime.c" \
                -o "$out/$name.bin" > "$out/$name.link.log" 2>&1; then
        # A negative case may legitimately fail at LINK time. Under rule R2 a
        # class's metadata is emitted strong by the TU that owns its
        # @implementation, so two TUs implementing the same class are a
        # duplicate symbol — a louder, earlier diagnostic than the runtime
        # __sig abort. Honour EXPECT_FAIL here too, or the case would report a
        # link failure as a regression.
        if [[ -f "$dir/EXPECT_FAIL" ]]; then
            local needle
            if [[ -f "$dir/EXPECT_FAIL_MATCH" ]]; then
                needle="$(cat "$dir/EXPECT_FAIL_MATCH")"
                if grep -qF "$needle" "$out/$name.link.log"; then
                    echo "PASS  $name (failed as expected at link: $(head -1 "$out/$name.link.log" | cut -c1-60))"
                    PASS=$((PASS+1)); return
                fi
                echo "FAIL  $name (link failed, but without the expected diagnostic)"
                echo "      expected to find: $needle"
                sed 's/^/      /' "$out/$name.link.log" | head -12
                FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
            fi
            echo "PASS  $name (failed as expected at link)"
            PASS=$((PASS+1)); return
        fi
        echo "FAIL  $name (link)"
        sed 's/^/      /' "$out/$name.link.log" | head -12
        FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
    fi
    fi

    # 3. run it. A mismatch in the vtable layout signature aborts here with a
    #    clear message; that counts as a FAIL, not a pass with warnings.
    "$out/$name.bin" > "$out/$name.run.log" 2>&1
    local rc=$?

    # A case may declare itself a negative one (EXPECT_FAIL in its directory):
    # the link/run is *supposed* to blow up. The contract is not merely "it
    # fails" but "it fails legibly" — the diagnostic must name the problem, so
    # a case that crashes for an unrelated reason does not pass by accident.
    if [[ -f "$dir/EXPECT_FAIL" ]]; then
        if [[ $rc -eq 0 ]]; then
            echo "FAIL  $name (expected a loud failure, but it ran clean)"
            sed 's/^/      /' "$out/$name.run.log" | head -14
            FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
        fi
        if [[ -f "$dir/EXPECT_FAIL_MATCH" ]]; then
            local needle; needle="$(cat "$dir/EXPECT_FAIL_MATCH")"
            if ! grep -qF "$needle" "$out/$name.run.log"; then
                echo "FAIL  $name (failed, but without the expected diagnostic)"
                echo "      expected to find: $needle"
                sed 's/^/      /' "$out/$name.run.log" | head -14
                FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
            fi
        fi
        echo "PASS  $name (failed as expected: $(head -1 "$out/$name.run.log" | cut -c1-60))"
        PASS=$((PASS+1)); return
    fi

    if [[ $rc -ne 0 ]]; then
        echo "FAIL  $name (run, exit $rc)"
        sed 's/^/      /' "$out/$name.run.log" | head -14
        FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
    fi

    # 4. optional output assertion
    if [[ -f "$dir/expected.txt" ]]; then
        if ! diff -u "$dir/expected.txt" "$out/$name.run.log" > "$out/$name.diff" 2>&1; then
            echo "FAIL  $name (output mismatch)"
            sed 's/^/      /' "$out/$name.diff" | head -20
            FAIL=$((FAIL+1)); FAILED_CASES+=("$name"); return
        fi
    fi

    echo "PASS  $name"
    PASS=$((PASS+1))
}

echo "using nopac: $NOPAC"
echo "using clang: $(clang --version | head -1)"
echo

shopt -s nullglob
for dir in "$CASES_ROOT"/[0-9][0-9]_*; do
    [[ -d "$dir" ]] || continue
    name="$(basename "$dir")"
    if [[ -n "$FILTER" && "$name" != *"$FILTER"* ]]; then continue; fi
    run_case "$dir"
done

echo "----"
echo "multi-TU: $PASS passed, $FAIL failed"
if [[ ${#FAILED_CASES[@]} -gt 0 ]]; then
    echo "failing cases: ${FAILED_CASES[*]}"
fi
exit $((FAIL > 0))
