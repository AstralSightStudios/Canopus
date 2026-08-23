#!/bin/sh
set -eu

TARGETS="
  xiaomi-band-10-pro-3.101.036
  xiaomi-band-10-pro-3.101.043
  xiaomi-band-9-3.1.32
"

SCRIPT_DIR="scripts"
BUILD_SCRIPT="$SCRIPT_DIR/build_canopus_supervisor.sh"

for target in $TARGETS; do
    echo "=========================================="
    echo "Building target: $target"
    echo "=========================================="

    CANOPUS_TARGET="$target" "$BUILD_SCRIPT"
done

echo "All targets built and staged successfully."
