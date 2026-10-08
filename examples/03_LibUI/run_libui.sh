#!/bin/bash
# examples/03_LibUI/run_libui.sh — transpile, compile, link, run the libui-ng demo.
#
# Pure Nepa: the only sources are .np/.nh files. The demo inlines the wrapper
# (include/LibUI.np → one .c file), which is compiled and linked with the
# Nepa runtime — no hand-written .c/.m files anywhere.
#
# Requires: libui-ng built with meson (set LIBUI_DIR to your checkout)
#           nepac (built at ../../target/debug/nepac or ../../target/release/nepac)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NEPALANG="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -n "${NEPAC:-}" ]; then
    NEPAC="$NEPAC"
elif [ -x "$NEPALANG/target/debug/nepac" ]; then
    NEPAC="$NEPALANG/target/debug/nepac"
else
    NEPAC="$NEPALANG/target/release/nepac"
fi

if [ -n "${LIBUI_DIR:-}" ]; then
    LIBUI="$LIBUI_DIR"
elif [ -d "$NEPALANG/../libui-ng" ]; then
    LIBUI="$(cd "$NEPALANG/../libui-ng" && pwd)"
else
    echo "Error: libui-ng not found. Set LIBUI_DIR to your libui-ng checkout." >&2
    exit 1
fi

BUILD=/tmp/libui_build
rm -rf "$BUILD"
mkdir -p "$BUILD"

echo "==> Transpile (nepac)..."
"$NEPAC" -rewrite-nepa "$SCRIPT_DIR/libui_demo.np" -o "$BUILD/libui_demo.c" \
    -I "$SCRIPT_DIR/include" -I "$LIBUI"

echo "==> Compile + link (clang)..."
FLAGS="-std=c99 -fblocks -w"
INCLUDES=(-I "$NEPALANG/include" -I "$NEPALANG/include/Foundation" -I "$LIBUI")
if [ "$(uname)" = "Darwin" ]; then
    FRAMEWORKS=(-framework Cocoa)
else
    FRAMEWORKS=()
fi

clang $FLAGS "${INCLUDES[@]}" \
    -x c "$BUILD/libui_demo.c" \
    "$NEPALANG/include/nepa/runtime.c" \
    -L "$LIBUI/build/meson-out" -lui \
    -Wl,-rpath,"$LIBUI/build/meson-out" \
    "${FRAMEWORKS[@]}" \
    -o "$BUILD/libui_demo"

echo "==> Run..."
"$BUILD/libui_demo"
