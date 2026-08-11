#!/bin/sh
# Build the exact Band-9 NSH stage-1/stage-2 bootstrap resources.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=xiaomi-band-9-pro-3.1.175
OUT="$ROOT/watchfaces/canopus-installer/build/$TARGET"
STAGE="$ROOT/watchfaces/canopus-installer"
SUPERVISOR="$OUT/canopus_supervisor.elf"
CC=${CC:-clang}

[ -f "$SUPERVISOR" ] || {
    echo "error: build Band-9 supervisor first" >&2
    exit 1
}
SIZE=$(wc -c < "$SUPERVISOR" | tr -d ' ')
FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft"
COMMON="-ffreestanding -fPIC -fno-common -fno-builtin -fno-jump-tables -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections -ffunction-sections -Os -Wall -Wextra -Werror"

$CC $FLAGS $COMMON -I"$ROOT/sdk/c" -I"$ROOT/runtime/loader" \
    -DCANOPUS_BAND9_SUPERVISOR_SIZE="$SIZE" \
    -c "$ROOT/manager/target/band9/canopus_band9_stage2.c" -o "$OUT/stage2.o"
$CC $FLAGS $COMMON -I"$ROOT/sdk/c" -I"$ROOT/runtime/loader" \
    -c "$ROOT/runtime/loader/canopus_arm_reloc.c" -o "$OUT/stage2-reloc.o"
$CC $FLAGS $COMMON -I"$ROOT/sdk/c" -I"$ROOT/runtime/loader" \
    -c "$ROOT/runtime/loader/canopus_elf32_loader.c" -o "$OUT/stage2-elf.o"
ld.lld -static --gc-sections --oformat=binary \
    -T "$ROOT/scripts/canopus_band9_stage2.ld" -o "$OUT/stage2.bin" \
    "$OUT/stage2.o" "$OUT/stage2-reloc.o" "$OUT/stage2-elf.o"

STAGE2_SIZE=$(wc -c < "$OUT/stage2.bin" | tr -d ' ')
$CC $FLAGS $COMMON -DCANOPUS_BAND9_STAGE2_SIZE="$STAGE2_SIZE" \
    -c "$ROOT/manager/target/band9/canopus_band9_stage1.c" -o "$OUT/stage1.o"
ld.lld -static --gc-sections --oformat=binary \
    -T "$ROOT/scripts/canopus_band9_stage1.ld" -o "$OUT/stage1.bin" \
    "$OUT/stage1.o"

cp "$SUPERVISOR" "$STAGE/canopus_supervisor-band9.bin"
cp "$OUT/stage2.bin" "$STAGE/canopus_stage2-band9.bin"
python3 "$ROOT/tools/band9-stage1-lua.py" "$OUT/stage1.bin" \
    "$STAGE/canopus_stage1_band9.lua"
echo "Band-9 bootstrap: stage1=$(wc -c < "$OUT/stage1.bin" | tr -d ' ') bytes stage2=$STAGE2_SIZE bytes supervisor=$SIZE bytes"
