#!/bin/sh
# Builds the Canopus supervisor native module for xiaomi-band-10-pro-3.101.030
# and stages it into watchfaces/canopus-installer/canopus_supervisor.bin.
#
# Uses the real device platform (register /dev/canopus via the stock
# register_driver, exactly like btpatch registers /dev/btpatch) so the
# installer watchface's status/command surface works on device. The module is
# a zero-import ELF32 ET_REL and must PASS the Canopus verifier.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET_ID="xiaomi-band-10-pro-3.101.030"
PACK_DIR="$ROOT/targets/$TARGET_ID"
GENERATED="$PACK_DIR/generated/canopus_veneer.h"
OUT="$ROOT/watchfaces/canopus-installer/build"
CC=${CC:-clang}

[ -f "$GENERATED" ] || {
    echo "error: run 'canopus target generate-veneer $TARGET_ID' first"
    exit 1
}

mkdir -p "$OUT"
cd "$ROOT"

echo "[1/3] compile supervisor (Cortex-M33 Thumb soft-float)"
# Flags mirror native/scripts/build_btpatch_phase5.sh; -fno-function-sections
# is btpatch's proven configuration for a boot-resident constructor module.
TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-jump-tables \
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fdata-sections -fno-function-sections -Os -Wall -Wextra -Werror"

INC="-I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/resources \
  -I$ROOT/runtime/diagnostics -I$ROOT/runtime/control -I$ROOT/runtime/module \
  -I$ROOT/manager/service -I$ROOT/manager/protocol -I$PACK_DIR/generated"

# The v2 transport (CAN-P0-008) pulls the protocol codec and the snapshot
# helpers into the module; both are freestanding (no libc).
for s in \
    manager/service/canopus_supervisor.c \
    manager/service/canopus_supervisor_module.c \
    manager/service/canopus_supervisor_platform.c \
    manager/protocol/canopus_protocol.c \
    runtime/control/canopus_control.c; do
    base=$(basename "$s")
    $CC $TARGET_FLAGS $INC -c "$ROOT/$s" -o "$OUT/${base%.c}.o"
done

echo "[2/3] relocatable link (ld.lld -r)"
ld.lld -r -o "$OUT/canopus_supervisor.elf" \
    "$OUT/canopus_supervisor.o" \
    "$OUT/canopus_supervisor_module.o" \
    "$OUT/canopus_supervisor_platform.o" \
    "$OUT/canopus_protocol.o" \
    "$OUT/canopus_control.o"

echo "[3/3] Canopus ELF verifier"
"$ROOT/target/debug/canopus" verify "$OUT/canopus_supervisor.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"

cp "$OUT/canopus_supervisor.elf" \
    "$ROOT/watchfaces/canopus-installer/canopus_supervisor.bin"
echo "staged canopus_supervisor.bin"
