#!/usr/bin/env bash
# tests/golden/26_baremetal_stress/build.sh
set -euo pipefail
cd "$(dirname "$0")"

GALDC=../../../target/debug/galdc
BUILD=build
mkdir -p "$BUILD"

echo "== transpiling =="
"$GALDC" -rewrite-gald -ffreestanding -o "$BUILD/stress.c" stress.gm

echo "== compiling transpiled C =="
clang -I../../../include -include gald/runtime.h \
    -D_FORTIFY_SOURCE=0 -Wno-unused-variable \
    -c "$BUILD/stress.c" -o "$BUILD/stress.o"

echo "== compiling helpers (kputs, kputdec, ...) =="
clang -I../../../include -U__GALD_FREESTANDING \
    -D_FORTIFY_SOURCE=0 \
    -c helpers.c -o "$BUILD/helpers.o"

echo "== compiling bare-metal runtime =="
clang -I../../../include -U__GALD_FREESTANDING \
    -D_FORTIFY_SOURCE=0 \
    -c ../../../include/gald/runtime_freestanding.c -o "$BUILD/runtime_freestanding.o"

echo "== linking =="
clang "$BUILD/stress.o" "$BUILD/helpers.o" "$BUILD/runtime_freestanding.o" -o "$BUILD/stress"

echo "== running =="
"$BUILD/stress"
echo "exit=$?"