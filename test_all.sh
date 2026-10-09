# ovel test_all.sh — delegate everything to parallel Python runner
# Usage: ./test_all.sh [-jN]

JOBS=1
ARGS=""
for arg in "$@"; do
    if [[ "$arg" =~ ^-j([0-9]+)$ ]]; then JOBS="${BASH_REMATCH[1]}"; fi
    ARGS="$ARGS $arg"
done

cd "$(dirname "$0")"
python3 test_all.py $ARGS
# Kill any leftover ovelc processes (orphaned if Python was killed by timeout)
pkill -f "target/debug/ovelc" 2>/dev/null || true
pkill -f "target/release/ovelc" 2>/dev/null || true
# Kill orphaned test binaries (compiled .ov executables left in /tmp/)
for f in tests/*.ov; do
    stem=$(basename "$f" .ov)
    pkill -f "^/tmp/$stem($| )" 2>/dev/null || true
done
