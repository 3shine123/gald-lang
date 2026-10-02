#!/usr/bin/env bash
# make-decl-headers.sh — turn a self-contained gald header into a
# declaration-only header by dropping every `#import "*.gm"` line.
#
#   ./tools/make-decl-headers.sh include/Foundation/Foundation.gh \
#                               include/Foundation/Foundation.decl.gh
#
# A self-contained header (`Foundation.gh`) inlines the implementation files
# (`.gm`) into every translation unit that imports it, so each TU regenerates
# the whole Foundation implementation. The declaration-only header keeps the
# same public surface but leaves the implementations to a precompiled library
# (`tools/build-foundation-lib.sh`) — the Objective-C model.
#
# The mechanically produced file is the header body; the committed
# `include/Foundation/Foundation.decl.gh` adds an explanatory comment block on
# top of it (this script does not invent or preserve prose).
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <source.gh> [output.gh]" >&2
    echo "       (output defaults to <source>.decl.gh)" >&2
    exit 2
fi

src="$1"
out="${2:-${src%.gh}.decl.gh}"

if [[ ! -f "$src" ]]; then
    echo "error: no such file: $src" >&2
    exit 1
fi

# Implementation inlines: `#import "....gm"` (relative or <...> form both end
# in .gm, so a single pattern catches them).
pat='^[[:space:]]*#import[[:space:]]+["<][^">]+\.gm[">]'
dropped="$(grep -c -E "$pat" "$src" || true)"

grep -v -E "$pat" "$src" > "$out"

echo "wrote $out"
echo "  source:  $src ($(wc -l < "$src" | tr -d ' ') lines)"
echo "  dropped: $dropped '#import \"*.gm\"' line(s)"
