#!/bin/bash
# examples/03_LibUI/run_libui.sh — transpile, compile, link, run the libui-ng demo.
#
# Pure Ovel: the only sources are .ov/.oh files. The demo inlines the wrapper
# (include/LibUI.ov → one .c file), which is compiled and linked with the
# Ovel runtime — no hand-written .c/.m files anywhere.
#
# Requires: libui-ng built with meson (set LIBUI_DIR to your checkout)
#           ovelc (built at ../../target/debug/ovelc or ../../target/release/ovelc)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OVELLANG="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -n "${OVELC:-}" ]; then
    OVELC="$OVELC"
elif [ -x "$OVELLANG/target/debug/ovelc" ]; then
    OVELC="$OVELLANG/target/debug/ovelc"
else
    OVELC="$OVELLANG/target/release/ovelc"
fi

if [ -n "${LIBUI_DIR:-}" ]; then
    LIBUI="$LIBUI_DIR"
elif [ -d "$OVELLANG/../libui-ng" ]; then
    LIBUI="$(cd "$OVELLANG/../libui-ng" && pwd)"
else
    echo "Error: libui-ng not found. Set LIBUI_DIR to your libui-ng checkout." >&2
    exit 1
fi

BUILD=/tmp/libui_build
rm -rf "$BUILD"
mkdir -p "$BUILD"

echo "==> Transpile (ovelc)..."
"$OVELC" -rewrite-ovel "$SCRIPT_DIR/libui_demo.ov" -o "$BUILD/libui_demo.c" \
    -I "$SCRIPT_DIR/include" -I "$LIBUI"

echo "==> Compile + link (clang)..."
FLAGS="-std=c99 -fblocks -w"
INCLUDES=(-I "$OVELLANG/include" -I "$OVELLANG/include/Foundation" -I "$LIBUI")
if [ "$(uname)" = "Darwin" ]; then
    FRAMEWORKS=(-framework Cocoa)
else
    FRAMEWORKS=()
fi

clang $FLAGS "${INCLUDES[@]}" \
    -x c "$BUILD/libui_demo.c" \
    "$OVELLANG/include/ovel/runtime.c" \
    -L "$LIBUI/build/meson-out" -lui \
    -Wl,-rpath,"$LIBUI/build/meson-out" \
    "${FRAMEWORKS[@]}" \
    -o "$BUILD/libui_demo"

echo "==> Run..."
"$BUILD/libui_demo"
