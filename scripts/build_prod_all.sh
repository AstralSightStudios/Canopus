#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
echo "Preparing exact-target verifier..."
cargo build -q -p canopus-cli
TARGETS=${CANOPUS_PROD_TARGETS:-$("$ROOT/target/debug/canopus" target list --active --names)}
BUILD_SCRIPT="$ROOT/scripts/build_canopus_supervisor.sh"


BAND11_REQUESTED=false
for target in $TARGETS; do
    echo "=========================================="
    echo "Building target: $target"
    echo "=========================================="

    case "$target" in
        xiaomi-band-11-*)
            BAND11_REQUESTED=true
            ;;
    esac
    CANOPUS_TARGET="$target" "$BUILD_SCRIPT"

done

if [ "$BAND11_REQUESTED" = true ]; then
    python3 "$ROOT/scripts/build_band11_installer.py" --check
    echo "Band 11 complete native installer resources built; device validation pending."
fi
echo "Requested artifacts built; native installation support remains target-specific."
