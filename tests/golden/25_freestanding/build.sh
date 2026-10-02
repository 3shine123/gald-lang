#!/usr/bin/env bash
# tests/golden/25_freestanding/build.sh
# Transpile + compile + run the bare-metal golden test on the HOST.
# The transpiled code uses -ffreestanding for codegen, but we compile it on
# the host with -include gald/runtime.h (pre-loads non-freestanding
# branch, so the file's own #include is guarded away) and
# -D_FORTIFY_SOURCE=0 to prevent macOS's fortified memcpy macro from
# conflicting with the runtime.h declarations.
set -euo pipefail
cd "$(dirname "$0")"

GALDC=../../../target/debug/galdc
BUILD=build
mkdir -p "$BUILD"

echo "== transpiling with -ffreestanding =="
"$GALDC" -rewrite-gald -ffreestanding -o "$BUILD/freestanding.c" freestanding.gm

echo "== compiling transpiled C (host, using libc setjmp) =="
clang -I../../../include -include gald/runtime.h \
    -D_FORTIFY_SOURCE=0 -Wno-unused-variable \
    -c "$BUILD/freestanding.c" -o "$BUILD/freestanding.o"

echo "== compiling helpers =="
clang -I../../../include -U__GALD_FREESTANDING \
    -D_FORTIFY_SOURCE=0 \
    -c helpers.c -o "$BUILD/helpers.o"

echo "== compiling bare-metal runtime (bump allocator) =="
clang -I../../../include -U__GALD_FREESTANDING \
    -D_FORTIFY_SOURCE=0 \
    -c ../../../include/gald/runtime_freestanding.c -o "$BUILD/runtime_freestanding.o"

echo "== linking =="
clang "$BUILD/freestanding.o" "$BUILD/helpers.o" "$BUILD/runtime_freestanding.o" -o "$BUILD/freestanding"

echo "== running =="
"$BUILD/freestanding"
echo "exit=$?"