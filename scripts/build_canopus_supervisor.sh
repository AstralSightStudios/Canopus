#!/bin/sh
# Builds the Canopus supervisor native module for xiaomi-band-10-pro-3.101.030
# and stages it into watchfaces/canopus-installer/canopus_supervisor.bin.
#
# The supervisor is a boot-resident char-device module (btpatch_phase5
# pattern). This build links the stub platform, so the module cross-compiles
# to a zero-import ELF32 ET_REL and PASSES the Canopus verifier — the G0 test
# artifact. Device registration (/dev/canopus) and module load/unload are
# device-gated (G0/G4): replace canopus_supervisor_platform_stub.c once the
# exact char-device API is recovered.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET_ID="xiaomi-band-10-pro-3.101.030"
OUT="$ROOT/watchfaces/canopus-installer/build"
CC=${CC:-clang}

mkdir -p "$OUT"
cd "$ROOT"

echo "[1/3] compile supervisor (Cortex-M33 Thumb soft-float)"
TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-stack-protector \
  -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections \
  -ffunction-sections -Os -Wall -Wextra -Werror"

INC="-I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/resources \
  -I$ROOT/runtime/diagnostics -I$ROOT/runtime/control -I$ROOT/runtime/module \
  -I$ROOT/manager/service"

for s in canopus_supervisor.c canopus_supervisor_module.c canopus_supervisor_platform_stub.c; do
    $CC $TARGET_FLAGS $INC -c "$ROOT/manager/service/$s" -o "$OUT/${s%.c}.o"
done

echo "[2/3] relocatable link (ld.lld -r)"
ld.lld -r -o "$OUT/canopus_supervisor.elf" \
    "$OUT/canopus_supervisor.o" \
    "$OUT/canopus_supervisor_module.o" \
    "$OUT/canopus_supervisor_platform_stub.o"

echo "[3/3] Canopus ELF verifier"
"$ROOT/target/debug/canopus" verify "$OUT/canopus_supervisor.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"

cp "$OUT/canopus_supervisor.elf" \
    "$ROOT/watchfaces/canopus-installer/canopus_supervisor.bin"
echo "staged canopus_supervisor.bin"
