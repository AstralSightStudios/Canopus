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
BAND9_TARGET="$BAND9_ROOT/targets/xiaomi-band-9-3.1.32"
for resource in \
    "$BAND9_ROOT/main.lua" \
    "$BAND9_ROOT/manager_icon.bin" \
    "$BAND9_TARGET/canopus_loader_profile.lua" \
    "$BAND9_TARGET/canopus_stage1.lua" \
    "$BAND9_TARGET/canopus_stage2.bin" \
    "$BAND9_TARGET/canopus_supervisor.bin"; do
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
    "$BAND9_TARGET/canopus_supervisor.bin" \
    --target xiaomi-band-9-3.1.32 --targets-dir "$ROOT/targets"
fi

echo "All targets built, verified, and staged successfully."
