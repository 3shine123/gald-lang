#!/usr/bin/env bash
# run_arc_intern_test.sh — regression guard for the ARC intern-table UAF.
#
# The failure is a heap-use-after-free that is invisible in a plain build
# (stdout is lost when the process aborts, so it looks like "no output, exit 1"),
# so this builds with AddressSanitizer and greps the report.
#
#   exit 0  -> clean: no sanitizer report and the expected output is produced
#   exit 1  -> the defect is back (or the repro stopped exercising it)
#
# Out-of-band (needs a sanitizer + a specific program); not part of test_all.py.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

galdc="${GALDC:-}"
if [[ -z "$galdc" ]]; then
    for cand in target/debug/galdc target/release/galdc; do
        if [[ -x "$cand" ]]; then galdc="$cand"; break; fi
    done
fi
if [[ -z "$galdc" || ! -x "$galdc" ]]; then
    echo "error: galdc not found (run 'cargo build', or set GALDC=/path/to/galdc)" >&2
    exit 2
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/gald_arc_intern.XXXXXX")"
trap 'rm -rf "$work"' EXIT

src="tests/arc_intern/arc_intern_uaf.gm"
bin="$work/repro"
log="$work/asan.log"

# Force the sanitizer through galdc's C-compiler override.
GALD_CC="${GALD_CC:-clang -fsanitize=address -g -O0}" \
    "$galdc" "$src" -I include -o "$bin" > "$work/build.log" 2>&1 || {
        echo "FAIL: build failed"; sed 's/^/  /' "$work/build.log" | tail -20; exit 1;
    }

rc=0
"$bin" > "$log" 2>&1 || rc=$?

if grep -q "AddressSanitizer" "$log"; then
    echo "FAIL: AddressSanitizer reported an error (intern-UAF regressed?)"
    grep -m1 "ERROR: AddressSanitizer" "$log" | sed 's/^/  /' || true
    exit 1
fi
if [[ $rc -ne 0 ]]; then
    echo "FAIL: binary exited $rc"
    sed 's/^/  /' "$log" | head -20
    exit 1
fi
if [[ "$(cat "$log")" != "k=second" ]]; then
    echo "FAIL: unexpected output"; sed 's/^/  /' "$log" | head -20
    exit 1
fi

echo "PASS: interned constant survives; no sanitizer report (k=second)"
