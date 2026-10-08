#!/usr/bin/env bash
# Build + run the x86_64 assembly fusion test via Rosetta.
# On this arm64 Mac, `-arch x86_64` cross-targets clang and the resulting
# Mach-O x86_64 binary is executed through Rosetta automatically.
set -euo pipefail
cd "$(dirname "$0")"

NEPAC="${NEPAC:-../target/debug/nepac}"

echo "==> transpile + compile as x86_64 + run (Rosetta)"
"$NEPAC" -arch x86_64 run -asm asm_x86_ext.s asm_x86_fusion_test.np