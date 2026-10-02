#!/bin/bash
# examples/03_LibUI/run_libui.sh — transpile, compile, link, run the libui-ng demo.
#
# Pure Nupa: the only sources are .gm/.gh files. The demo inlines the wrapper
# (include/LibUI.gm → one .c file), which is compiled and linked with the
# Nupa runtime — no hand-written .c/.m files anywhere.
#
# Requires: libui-ng built with meson (set LIBUI_DIR to your checkout)
#           galdc (built at ../../target/debug/galdc or ../../target/release/galdc)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NUPALANG="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -n "${GALDC:-}" ]; then
    GALDC="$GALDC"
elif [ -x "$NUPALANG/target/debug/galdc" ]; then
    GALDC="$NUPALANG/target/debug/galdc"
else
    GALDC="$NUPALANG/target/release/galdc"
fi

if [ -n "${LIBUI_DIR:-}" ]; then
    LIBUI="$LIBUI_DIR"
elif [ -d "$NUPALANG/../libui-ng" ]; then
    LIBUI="$(cd "$NUPALANG/../libui-ng" && pwd)"
else
    echo "Error: libui-ng not found. Set LIBUI_DIR to your libui-ng checkout." >&2
    exit 1
fi

BUILD=/tmp/libui_build
rm -rf "$BUILD"
mkdir -p "$BUILD"

echo "==> Transpile (galdc)..."
"$GALDC" -rewrite-gald "$SCRIPT_DIR/libui_demo.gm" -o "$BUILD/libui_demo.c" \
    -I "$SCRIPT_DIR/include" -I "$LIBUI"

echo "==> Compile + link (clang)..."
FLAGS="-std=c99 -fblocks -w"
INCLUDES=(-I "$NUPALANG/include" -I "$NUPALANG/include/Foundation" -I "$LIBUI")
if [ "$(uname)" = "Darwin" ]; then
    FRAMEWORKS=(-framework Cocoa)
else
    FRAMEWORKS=()
fi

clang $FLAGS "${INCLUDES[@]}" \
    -x c "$BUILD/libui_demo.c" \
    "$NUPALANG/include/gald/runtime.c" \
    -L "$LIBUI/build/meson-out" -lui \
    -Wl,-rpath,"$LIBUI/build/meson-out" \
    "${FRAMEWORKS[@]}" \
    -o "$BUILD/libui_demo"

echo "==> Run..."
"$BUILD/libui_demo"
