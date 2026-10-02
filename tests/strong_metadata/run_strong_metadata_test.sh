#!/usr/bin/env bash
# run_strong_metadata_test.sh — protection tests for -fstrong-metadata.
#
# Guards the precompiled-library (P3) contract from doc/stable_slots_plan.md §8
# on three levels, because "the program printed the right thing" alone does not
# prove the metadata linkage is right:
#
#   1. OBJECT FILE — the library's class metadata is actually STRONG
#      (nm), not merely weak-merged by luck. Reuses tools/build-foundation-lib.sh,
#      which also enforces this.
#   2. LINK + RUN  — a client TU that only sees declarations (weak stubs) links
#      against the strong library and runs correctly.
#   3. LOUD FAILURE — if a client is *also* compiled with -fstrong-metadata the
#      link must report a duplicate symbol, instead of silently picking one
#      table at random.
#
# Out-of-band (like tests/multi_tu): not part of test_all.py.
set -uo pipefail

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

work="$(mktemp -d "${TMPDIR:-/tmp}/gald_strong_meta.XXXXXX")"
trap 'rm -rf "$work"' EXIT

pass=0; fail=0
ok()   { echo "PASS  $1"; pass=$((pass+1)); }
bad()  { echo "FAIL  $1"; fail=$((fail+1)); }

lib="$work/lib"
client="$work/client.gm"
cat > "$client" <<'EOF'
#import <Foundation/Foundation.decl.gh>
#include <stdio.h>
int main() {
    @autoreleasepool {
        NFString *s = [NFString stringWithUTF8String:"hello"];
        printf("len=%zu s=%s\n", [s length], [s UTF8String]);
    }
    return 0;
}
EOF

# ── 1. library metadata is strong (check 1 lives inside the build script) ──
if GALDC="$galdc" ./tools/build-foundation-lib.sh "$lib" > "$work/lib.log" 2>&1; then
    ok "1. library built with strong metadata (nm verified by the build script)"
else
    bad "1. library build / strong-metadata verification"
    sed 's/^/      /' "$work/lib.log" | tail -20
fi

# ── 2. weak client + strong library: link and run ──
if "$galdc" "$client" -I include -L "$lib" -lgaldfoundation -o "$work/app" > "$work/app.log" 2>&1; then
    out="$("$work/app" 2>&1)"; rc=$?
    if [[ $rc -eq 0 && "$out" == *"len=5 s=hello"* ]]; then
        ok "2. declaration-only client links against the strong library and runs"
    else
        bad "2. client ran but produced the wrong result (rc=$rc: $out)"
    fi
else
    bad "2. client failed to link against the strong library"
    sed 's/^/      /' "$work/app.log" | tail -20
fi

# ── 3. strong client + strong library: the link must fail loudly ──
if "$galdc" -fstrong-metadata "$client" -I include -L "$lib" -lgaldfoundation -o "$work/app_dup" > "$work/dup.log" 2>&1; then
    bad "3. two strong-metadata TUs linked without error (should be a duplicate symbol)"
else
    if grep -q "duplicate symbol" "$work/dup.log"; then
        ok "3. two strong-metadata TUs are rejected with 'duplicate symbol'"
    else
        bad "3. link failed, but not with a duplicate-symbol diagnostic"
        sed 's/^/      /' "$work/dup.log" | tail -20
    fi
fi

echo "----"
echo "strong-metadata: $pass passed, $fail failed"
echo
echo "note: two WEAK TUs that both '@implementation' the same class still merge"
echo "      silently (method bodies and metadata are emitted weak by design —"
echo "      crates/codegen/src/codegen.rs). Making that a loud error is the"
echo "      deferred per-class-ownership rule (R2), not something this flag"
echo "      changes; test 3 is the loud failure the flag does give you."
exit $((fail > 0))
