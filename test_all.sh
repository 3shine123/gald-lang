# nepa test_all.sh — delegate everything to parallel Python runner
# Usage: ./test_all.sh [-jN]

JOBS=1
ARGS=""
for arg in "$@"; do
    if [[ "$arg" =~ ^-j([0-9]+)$ ]]; then JOBS="${BASH_REMATCH[1]}"; fi
    ARGS="$ARGS $arg"
done

cd "$(dirname "$0")"
python3 test_all.py $ARGS
# Kill any leftover nepac processes (orphaned if Python was killed by timeout)
pkill -f "target/debug/nepac" 2>/dev/null || true
pkill -f "target/release/nepac" 2>/dev/null || true
# Kill orphaned test binaries (compiled .np executables left in /tmp/)
for f in tests/*.np; do
    stem=$(basename "$f" .np)
    pkill -f "^/tmp/$stem($| )" 2>/dev/null || true
done
