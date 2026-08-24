#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGETS=${CANOPUS_PROD_TARGETS:-"
  xiaomi-band-10-pro-3.101.036
  xiaomi-band-10-pro-3.101.043
  xiaomi-band-9-3.1.32
"}
BUILD_SCRIPT="$ROOT/scripts/build_canopus_supervisor.sh"

cd "$ROOT"
echo "Preparing exact-target verifier..."
cargo build -p canopus-cli

BAND9_REQUESTED=false
for target in $TARGETS; do
    echo "=========================================="
    echo "Building target: $target"
    echo "=========================================="

    case "$target" in
        xiaomi-band-9-*) BAND9_REQUESTED=true ;;
    esac
    CANOPUS_TARGET="$target" "$BUILD_SCRIPT"

done

if [ "$BAND9_REQUESTED" = true ]; then
BAND9_ROOT="$ROOT/watchfaces/canopus-installer-prod/xiaomi-band-9"
BAND9_TARGET=xiaomi-band-9-3.1.32
for resource in \
    "$BAND9_ROOT/main.lua" \
    "$BAND9_ROOT/manager_icon.bin" \
    "$BAND9_ROOT/canopus_loader_profile-$BAND9_TARGET.bin" \
    "$BAND9_ROOT/canopus_stage1-$BAND9_TARGET.bin" \
    "$BAND9_ROOT/canopus_stage2-$BAND9_TARGET.bin" \
    "$BAND9_ROOT/canopus_supervisor-$BAND9_TARGET.bin"; do
    [ -s "$resource" ] || {
        echo "error: incomplete Band 9 prod resource: $resource" >&2
        exit 1
    }
done
cmp "$ROOT/watchfaces/canopus-installer/manager_icon.bin" \
    "$BAND9_ROOT/manager_icon.bin" >/dev/null || {
    echo "error: Band 9 prod Manager icon is stale" >&2
    exit 1
}

"$ROOT/target/debug/canopus" verify \
    "$BAND9_ROOT/canopus_supervisor-$BAND9_TARGET.bin" \
    --target "$BAND9_TARGET" --targets-dir "$ROOT/targets"
fi

echo "All targets built, verified, and staged successfully."
