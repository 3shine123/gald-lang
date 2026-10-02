#!/usr/bin/env bash
# tests/stress/interop/build.sh
# Three-way interop: Gald (lib.gm → lib.c + bridge lib.h) + plain C
# (caller.c, helper.c) + ARM64 assembly (asm_lib.s) + gald runtime.c,
# all linked into one binary by clang.
set -euo pipefail
cd "$(dirname "$0")"

GALDC=../../../target/debug/galdc
BUILD=build
mkdir -p "$BUILD"

echo "== transpile gald → C + bridge header =="
"$GALDC" -rewrite-gald lib.gm -o "$BUILD/lib.c" -emit-bridge-header "$BUILD/lib.h"

echo "== assemble (separate step — never mix .s into the C compile) =="
clang -c asm_lib.s -o "$BUILD/asm.o"

echo "== compile + link: C caller + gald C + helpers + asm.o + runtime =="
# Generated lib.c is hosted-mode: it does NOT include the runtime header
# itself, so inject it with -include (same as the freestanding stress does).
# -I build lets caller.c's `#include "lib.h"` resolve to the bridge header.
# The .s is pre-assembled because -include gald/runtime.h must not be
# applied to assembly files (forced C headers break .s parsing).
clang -I../../../include -I"$BUILD" -include gald/runtime.h \
    -Wno-unused-variable \
    "$BUILD/lib.c" \
    caller.c \
    helper.c \
    "$BUILD/asm.o" \
    ../../../include/gald/runtime.c \
    -o "$BUILD/interop_test"

echo "== run =="
"$BUILD/interop_test"
echo "exit=$?"
