#!/usr/bin/env bash
# build-foundation-lib.sh — build the precompiled Foundation library (P3).
#
#   ./tools/build-foundation-lib.sh [outdir]        # default: target/foundation
#
# Produces <outdir>/libgaldfoundation.a, to be linked by a program that
# `#import <Foundation/Foundation.decl.gh>`:
#
#   galdc app.gm -I include -L<outdir> -lgaldfoundation -o app
#
# WHY -fstrong-metadata IS MANDATORY
# ----------------------------------
# A client TU that only sees declarations still synthesizes metadata stubs
# (NULL-filled vtables) for every class it can name. Those stubs are weak.
# Without -fstrong-metadata the library's metadata is weak too, so the linker
# may keep a client's stub instead of the library's real table and dispatch
# segfaults at runtime (exit 139). The flag makes the library's metadata strong
# so it always wins. See doc/stable_slots_plan.md §8.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
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

outdir="${1:-target/foundation}"
mkdir -p "$outdir"

header="include/Foundation/Foundation.gh"
c="$outdir/Foundation.c"
o="$outdir/Foundation.o"
lib="$outdir/libgaldfoundation.a"

echo "[1/4] transpile $header"
"$galdc" -rewrite-gald -fstrong-metadata "$header" -o "$c" -I include

echo "[2/4] compile"
clang -c "$c" -o "$o" -I include

echo "[3/4] archive"
rm -f "$lib"
ar rcs "$lib" "$o"

echo "[4/4] verify metadata is strong (not weak)"
# If any of these is weak, -fstrong-metadata did not take effect and a client
# TU's stub could win the link — fail loudly instead of shipping a landmine.
#
# Weakness is platform-specific in nm output:
#   * Mach-O (Darwin): plain nm shows a weak definition as 'D'/'T' anyway, so
#     `nm -m` and its "weak external" marker are required.
#   * ELF (Linux): weak symbols carry 'W'/'V'/'w'/'v'.
nm_is_weak() {
    local obj="$1" sym="$2"
    case "$(uname -s)" in
        Darwin)
            nm -m "$obj" 2>/dev/null | grep -F -- " $sym" | grep -q "weak external"
            ;;
        *)
            nm "$obj" 2>/dev/null | grep -F -- " $sym" \
                | grep -qE '^[0-9a-fA-F]+[[:space:]]+[WwVv][[:space:]]'
            ;;
    esac
}

check_strong() {
    local sym="$1"
    if ! nm "$o" 2>/dev/null | grep -qF -- " $sym"; then
        echo "  error: symbol $sym not found in $o" >&2
        exit 1
    fi
    if nm_is_weak "$o" "$sym"; then
        echo "  error: $sym is WEAK — -fstrong-metadata did not apply" >&2
        exit 1
    fi
    echo "  ok: $sym"
}

check_strong '_GALD_VTABLE_$_NFString'
check_strong '_GALD_META_VTABLE_$_NFString_inst'
check_strong '_GALD_GETCLASS_$_NFString'
check_strong '_gald_metaInit'
check_strong '_gald_meta_init'

echo "done: $lib ($(du -h "$lib" | cut -f1))"
