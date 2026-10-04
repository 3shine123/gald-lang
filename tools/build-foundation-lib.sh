#!/usr/bin/env bash
# build-foundation-lib.sh — build the precompiled Foundation library (Plan A).
#
#   ./tools/build-foundation-lib.sh [outdir]        # default: target/foundation
#
# Per-TU compilation (doc/stable_slots_plan.md §11, plan A): every Foundation
# `.gm` is compiled as its OWN translation unit — i.e. as a main file — so R2
# ownership makes each owned class's metadata STRONG automatically. No
# -fstrong-metadata anywhere.
#
#   galdc app.gm -I include -L<outdir> -lgaldfoundation -o app
#
# WHY PER-TU WORKS (and the old single-TU build needed the flag)
# --------------------------------------------------------------
# Building `Foundation.gh` as one TU inlines every implementation through
# `#import "*.gm"`; none of them is the main file, so ALL metadata came out
# weak and a client's declaration-only stub could win the link (exit 139) —
# that is why the old script mandated -fstrong-metadata. Compiling each .gm
# separately makes its @implementation the main file: the owned class gets
# strong metadata (plus the §10 statically-initialized strong NFClass), while
# implementations reached via #import stay weak non-owner copies that merge
# harmlessly with the owner's strong tables. `gald_metaInit` stays weak and
# only back-fills what static initialization did not already cover.
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
lib="$outdir/libgaldfoundation.a"

# Mach-O prepends `_` to every C symbol; ELF uses the name as written.
sym_prefix=""
case "$(uname -s)" in
    Darwin) sym_prefix="_" ;;
esac

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
    local obj="$1" cls="$2"
    for member in "GALD_VTABLE_\$_${cls}" "GALD_META_VTABLE_\$_${cls}_inst" "GALD_GETCLASS_\$_${cls}"; do
        local sym="${sym_prefix}${member}"
        if ! nm "$obj" 2>/dev/null | grep -qF -- " $sym"; then
            echo "  error: symbol $sym not found in $obj" >&2
            exit 1
        fi
        if nm_is_weak "$obj" "$sym"; then
            echo "  error: $sym is WEAK — R2 ownership did not apply (is $cls really implemented in $(basename "$obj" .o).gm?)" >&2
            exit 1
        fi
        echo "  ok: $sym"
    done
}

echo "[1/4] transpile + compile each Foundation TU (wrapper = decl surface + implementation)"
# Each TU is a generated WRAPPER: the full declaration surface
# (Foundation.decl.gh) first, then the implementation .gm's own text — the
# @implementation lands in the MAIN FILE (not via #import), so R2 ownership
# still applies and this TU's metadata is strong. The full surface gives
# every TU the same shared vtable segment as a client (same __sig), and stub
# references to sibling implementations resolve from the archive at final
# link. The source .gm files stay UNPOLLUTED — self-contained TUs that inline
# them keep exactly the declaration surface they asked for.
# Note: NFObject.gm imports "NFObject.gh" in quoted form; the extra
# -I include/Foundation keeps that resolvable from the wrapper's location.
objs=()
tus="$outdir/_tus"
mkdir -p "$tus"
for gm in include/Foundation/*.gm; do
    name="$(basename "$gm" .gm)"
    wrap="$tus/$name.gm"
    { echo '#import <Foundation/Foundation.decl.gh>'; cat "$gm"; } > "$wrap"
    c="$outdir/$name.c"
    o="$outdir/$name.o"
    "$galdc" -rewrite-gald "$wrap" -o "$c" -I include -I include/Foundation
    clang -c -w "$c" -o "$o" -I include
    objs+=("$o")
    echo "  $name"
done

echo "[2/4] archive"
rm -f "$lib"
ar rcs "$lib" "${objs[@]}"

echo "[3/4] verify per-TU owned metadata is strong (nm)"
# Every Foundation .gm implements exactly the class it is named after, so the
# basename IS the owned class. If any of these is weak, ownership did not
# apply and a client TU's stub could win the link — fail loudly instead of
# shipping a landmine.
for gm in include/Foundation/*.gm; do
    name="$(basename "$gm" .gm)"
    check_strong "$outdir/$name.o" "$name"
done

echo "done: $lib ($(du -h "$lib" | cut -f1))"
