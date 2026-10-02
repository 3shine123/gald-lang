#!/usr/bin/env bash
# tests/golden/26_baremetal_stress/build.sh
set -euo pipefail
cd "$(dirname "$0")"

NUPAC=../../../target/debug/nupac
BUILD=build
mkdir -p "$BUILD"

echo "== transpiling =="
"$NUPAC" -rewrite-nupa -ffreestanding -o "$BUILD/stress.c" stress.np

echo "== compiling transpiled C =="
clang -I../../../include -include nupa/runtime.h \
    -D_FORTIFY_SOURCE=0 -Wno-unused-variable \
    -c "$BUILD/stress.c" -o "$BUILD/stress.o"

echo "== compiling helpers (kputs, kputdec, ...) =="
clang -I../../../include -U__NUPA_FREESTANDING \
    -D_FORTIFY_SOURCE=0 \
    -c helpers.c -o "$BUILD/helpers.o"

echo "== compiling bare-metal runtime =="
clang -I../../../include -U__NUPA_FREESTANDING \
    -D_FORTIFY_SOURCE=0 \
    -c ../../../include/nupa/runtime_freestanding.c -o "$BUILD/runtime_freestanding.o"

echo "== linking =="
clang "$BUILD/stress.o" "$BUILD/helpers.o" "$BUILD/runtime_freestanding.o" -o "$BUILD/stress"

echo "== running =="
"$BUILD/stress"
echo "exit=$?"