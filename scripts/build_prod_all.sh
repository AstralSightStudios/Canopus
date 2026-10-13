#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGETS=${CANOPUS_PROD_TARGETS:-"
  xiaomi-band-10-pro-3.101.036
  xiaomi-band-10-pro-3.101.043
  xiaomi-band-11-4.100.139
  xiaomi-band-11-4.100.155
  xiaomi-band-11-4.100.177
"}
BUILD_SCRIPT="$ROOT/scripts/build_canopus_supervisor.sh"

cd "$ROOT"
echo "Preparing exact-target verifier..."
cargo build -p canopus-cli

BAND11_REQUESTED=false
for target in $TARGETS; do
    echo "=========================================="
    echo "Building target: $target"
    echo "=========================================="

    case "$target" in
        xiaomi-band-11-4.100.139|xiaomi-band-11-4.100.155|xiaomi-band-11-4.100.177)
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
