#!/usr/bin/env bash
# tests/stress/interop/build.sh
# Three-way interop: Ovic (lib.ov → lib.c + bridge lib.h) + plain C
# (caller.c, helper.c) + ARM64 assembly (asm_lib.s) + ovic runtime.c,
# all linked into one binary by clang.
set -euo pipefail
cd "$(dirname "$0")"

OVICC=../../../target/debug/ovicc
BUILD=build
mkdir -p "$BUILD"

echo "== transpile ovic → C + bridge header =="
"$OVICC" -rewrite-ovic lib.ov -o "$BUILD/lib.c" -emit-bridge-header "$BUILD/lib.h"

echo "== assemble (separate step — never mix .s into the C compile) =="
clang -c asm_lib.s -o "$BUILD/asm.o"

echo "== compile + link: C caller + ovic C + helpers + asm.o + runtime =="
# Generated lib.c is hosted-mode: it does NOT include the runtime header
# itself, so inject it with -include (same as the freestanding stress does).
# -I build lets caller.c's `#include "lib.h"` resolve to the bridge header.
# The .s is pre-assembled because -include ovic/runtime.h must not be
# applied to assembly files (forced C headers break .s parsing).
clang -I../../../include -I"$BUILD" -include ovic/runtime.h \
    -Wno-unused-variable \
    "$BUILD/lib.c" \
    caller.c \
    helper.c \
    "$BUILD/asm.o" \
    ../../../include/ovic/runtime.c \
    -o "$BUILD/interop_test"

echo "== run =="
"$BUILD/interop_test"
echo "exit=$?"
