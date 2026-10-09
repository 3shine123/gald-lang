#!/bin/bash
# examples/03_LibUI/run_libui.sh — transpile, compile, link, run the libui-ng demo.
#
# Pure Ovic: the only sources are .ov/.oh files. The demo inlines the wrapper
# (include/LibUI.ov → one .c file), which is compiled and linked with the
# Ovic runtime — no hand-written .c/.m files anywhere.
#
# Requires: libui-ng built with meson (set LIBUI_DIR to your checkout)
#           ovicc (built at ../../target/debug/ovicc or ../../target/release/ovicc)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OVICLANG="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -n "${OVICC:-}" ]; then
    OVICC="$OVICC"
elif [ -x "$OVICLANG/target/debug/ovicc" ]; then
    OVICC="$OVICLANG/target/debug/ovicc"
else
    OVICC="$OVICLANG/target/release/ovicc"
fi

if [ -n "${LIBUI_DIR:-}" ]; then
    LIBUI="$LIBUI_DIR"
elif [ -d "$OVICLANG/../libui-ng" ]; then
    LIBUI="$(cd "$OVICLANG/../libui-ng" && pwd)"
else
    echo "Error: libui-ng not found. Set LIBUI_DIR to your libui-ng checkout." >&2
    exit 1
fi

BUILD=/tmp/libui_build
rm -rf "$BUILD"
mkdir -p "$BUILD"

echo "==> Transpile (ovicc)..."
"$OVICC" -rewrite-ovic "$SCRIPT_DIR/libui_demo.ov" -o "$BUILD/libui_demo.c" \
    -I "$SCRIPT_DIR/include" -I "$LIBUI"

echo "==> Compile + link (clang)..."
FLAGS="-std=c99 -fblocks -w"
INCLUDES=(-I "$OVICLANG/include" -I "$OVICLANG/include/Foundation" -I "$LIBUI")
if [ "$(uname)" = "Darwin" ]; then
    FRAMEWORKS=(-framework Cocoa)
else
    FRAMEWORKS=()
fi

clang $FLAGS "${INCLUDES[@]}" \
    -x c "$BUILD/libui_demo.c" \
    "$OVICLANG/include/ovic/runtime.c" \
    -L "$LIBUI/build/meson-out" -lui \
    -Wl,-rpath,"$LIBUI/build/meson-out" \
    "${FRAMEWORKS[@]}" \
    -o "$BUILD/libui_demo"

echo "==> Run..."
"$BUILD/libui_demo"
