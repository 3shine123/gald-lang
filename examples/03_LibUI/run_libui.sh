#!/bin/bash
# examples/03_LibUI/run_libui.sh — transpile, compile, link, run the libui-ng demo.
#
# Pure Nopa: the only sources are .np/.nh files. The demo inlines the wrapper
# (include/LibUI.np → one .c file), which is compiled and linked with the
# Nopa runtime — no hand-written .c/.m files anywhere.
#
# Requires: libui-ng built with meson (set LIBUI_DIR to your checkout)
#           nopac (built at ../../target/debug/nopac or ../../target/release/nopac)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NOPALANG="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -n "${NOPAC:-}" ]; then
    NOPAC="$NOPAC"
elif [ -x "$NOPALANG/target/debug/nopac" ]; then
    NOPAC="$NOPALANG/target/debug/nopac"
else
    NOPAC="$NOPALANG/target/release/nopac"
fi

if [ -n "${LIBUI_DIR:-}" ]; then
    LIBUI="$LIBUI_DIR"
elif [ -d "$NOPALANG/../libui-ng" ]; then
    LIBUI="$(cd "$NOPALANG/../libui-ng" && pwd)"
else
    echo "Error: libui-ng not found. Set LIBUI_DIR to your libui-ng checkout." >&2
    exit 1
fi

BUILD=/tmp/libui_build
rm -rf "$BUILD"
mkdir -p "$BUILD"

echo "==> Transpile (nopac)..."
"$NOPAC" -rewrite-nopa "$SCRIPT_DIR/libui_demo.np" -o "$BUILD/libui_demo.c" \
    -I "$SCRIPT_DIR/include" -I "$LIBUI"

echo "==> Compile + link (clang)..."
FLAGS="-std=c99 -fblocks -w"
INCLUDES=(-I "$NOPALANG/include" -I "$NOPALANG/include/Foundation" -I "$LIBUI")
if [ "$(uname)" = "Darwin" ]; then
    FRAMEWORKS=(-framework Cocoa)
else
    FRAMEWORKS=()
fi

clang $FLAGS "${INCLUDES[@]}" \
    -x c "$BUILD/libui_demo.c" \
    "$NOPALANG/include/nopa/runtime.c" \
    -L "$LIBUI/build/meson-out" -lui \
    -Wl,-rpath,"$LIBUI/build/meson-out" \
    "${FRAMEWORKS[@]}" \
    -o "$BUILD/libui_demo"

echo "==> Run..."
"$BUILD/libui_demo"
