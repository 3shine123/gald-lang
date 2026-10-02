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

# ── 4. rule R2: two owners of the same class must not merge silently ──
# No flag involved: ownership is derived from the main file, so two TUs that
# both `@implementation` Widget are both owners and the link must reject them.
cat > "$work/model.gh" <<'EOF'
@interface Widget
- (void)show;
@end
EOF
cat > "$work/lib_a.gm" <<'EOF'
#import "model.gh"
@implementation Widget
- (void)show { }
@end
EOF
cat > "$work/lib_b.gm" <<'EOF'
#import "model.gh"
@implementation Widget
- (void)show { }
@end
EOF
cat > "$work/driver.gm" <<'EOF'
#import "model.gh"
int main() { Widget *w = 0; if (w) { [w show]; } return 0; }
EOF
if "$galdc" -rewrite-gald "$work/lib_a.gm" -I "$work" -I include -o "$work/a.c" > "$work/a.log" 2>&1 \
   && "$galdc" -rewrite-gald "$work/lib_b.gm" -I "$work" -I include -o "$work/b.c" > "$work/b.log" 2>&1 \
   && "$galdc" -rewrite-gald "$work/driver.gm" -I "$work" -I include -o "$work/driver.c" >> "$work/a.log" 2>&1; then
    if clang -std=c99 -w "$work/a.c" "$work/b.c" "$work/driver.c" include/gald/runtime.c \
            -I include -o "$work/dup_impl" > "$work/dup_impl.log" 2>&1; then
        bad "4. two TUs owning the same class linked without error (R2 regression)"
    elif grep -q "duplicate symbol" "$work/dup_impl.log"; then
        ok "4. two TUs owning the same class are rejected with 'duplicate symbol' (R2)"
    else
        bad "4. link failed, but not with a duplicate-symbol diagnostic"
        sed 's/^/      /' "$work/dup_impl.log" | tail -12
    fi
else
    bad "4. could not transpile the two-owner case"
    sed 's/^/      /' "$work/a.log" "$work/b.log" | tail -12
fi

echo "----"
echo "strong-metadata: $pass passed, $fail failed"
exit $((fail > 0))
