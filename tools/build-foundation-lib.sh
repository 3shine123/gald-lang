#!/usr/bin/env bash
# build-foundation-lib.sh — build the precompiled Foundation library (Plan A).
#
#   ./tools/build-foundation-lib.sh [outdir]        # default: target/foundation
#
# Per-TU compilation (doc/stable_slots_plan.md §11, plan A): every Foundation
# `.np` is compiled as its OWN translation unit — i.e. as a main file — so R2
# ownership makes each owned class's metadata STRONG automatically. No
# -fstrong-metadata anywhere.
#
#   nopac app.np -I include -L<outdir> -lnopafoundation -o app
#
# The client imports `Foundation.nh` — the DECLARATION-ONLY umbrella (per the
# project's .nh = declarations / .np = implementations convention).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

nopac="${NOPAC:-}"
if [[ -z "$nopac" ]]; then
    for cand in target/debug/nopac target/release/nopac; do
        if [[ -x "$cand" ]]; then nopac="$cand"; break; fi
    done
fi
if [[ -z "$nopac" || ! -x "$nopac" ]]; then
    echo "error: nopac not found (run 'cargo build', or set NOPAC=/path/to/nopac)" >&2
    exit 2
fi

outdir="${1:-target/foundation}"
mkdir -p "$outdir"
lib="$outdir/libnopafoundation.a"

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
    for member in "NOPA_VTABLE_\$_${cls}" "NOPA_META_VTABLE_\$_${cls}_inst" "NOPA_GETCLASS_\$_${cls}"; do
        local sym="${sym_prefix}${member}"
        if ! nm "$obj" 2>/dev/null | grep -qF -- " $sym"; then
            echo "  error: symbol $sym not found in $obj" >&2
            exit 1
        fi
        if nm_is_weak "$obj" "$sym"; then
            echo "  error: $sym is WEAK — R2 ownership did not apply (is $cls really implemented in $(basename "$obj" .o).np?)" >&2
            exit 1
        fi
        echo "  ok: $sym"
    done
}

echo "[1/4] transpile + compile each Foundation TU (wrapper = decl surface + implementation)"
# Each TU is a generated WRAPPER: the full declaration surface
# (Foundation.nh, the declaration-only umbrella) first, then the
# implementation .np's own text — the
# @implementation lands in the MAIN FILE (not via #import), so R2 ownership
# still applies and this TU's metadata is strong. The full surface gives
# every TU the same shared vtable segment as a client (same __sig), and stub
# references to sibling implementations resolve from the archive at final
# link. The source .np files stay UNPOLLUTED — self-contained TUs that inline
# them keep exactly the declaration surface they asked for.
# Note: NFObject.np imports "NFObject.nh" in quoted form; the extra
# -I include/Foundation keeps that resolvable from the wrapper's location.
objs=()
tus="$outdir/_tus"
rm -rf "$tus"   # stale wrappers from an interrupted earlier run must not survive
mkdir -p "$tus"
rm -f "$outdir"/*.o   # stale objects from an earlier run must not linger into [3/4]
for gm in include/Foundation/*.np; do
    name="$(basename "$gm" .np)"
    # Skip the self-contained umbrella (Foundation.np): it is not a class —
    # it has no vtable to verify, and archiving it would re-inline every
    # implementation into the library, defeating the per-TU build.
    [[ "$name" == "Foundation" ]] && continue
    wrap="$tus/$name.np"
    { echo '#import <Foundation/Foundation.nh>'; cat "$gm"; } > "$wrap"
    c="$outdir/$name.c"
    o="$outdir/$name.o"
    "$nopac" -rewrite-nopa "$wrap" -o "$c" -I include -I include/Foundation
    clang -c -w "$c" -o "$o" -I include
    objs+=("$o")
    echo "  $name"
done

echo "[2/4] archive"
rm -f "$lib"
ar rcs "$lib" "${objs[@]}"

echo "[3/4] verify per-TU owned metadata is strong (nm)"
# Every Foundation .np implements exactly the class it is named after, so the
# basename IS the owned class. If any of these is weak, ownership did not
# apply and a client TU's stub could win the link — fail loudly instead of
# shipping a landmine.
for gm in include/Foundation/*.np; do
    name="$(basename "$gm" .np)"
    [[ "$name" == "Foundation" ]] && continue   # umbrella has no vtable (see [1/4])
    check_strong "$outdir/$name.o" "$name"
done

echo "done: $lib ($(du -h "$lib" | cut -f1))"
