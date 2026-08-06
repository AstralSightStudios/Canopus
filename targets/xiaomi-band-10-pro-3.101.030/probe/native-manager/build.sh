#!/bin/sh
# Build the resident exact-target native Manager with its semantic UI backend.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
TARGET_ID="xiaomi-band-10-pro-3.101.030"
PACK="$ROOT/targets/$TARGET_ID"
OUT="$PACK/probe/native-manager/build"
CC=${CC:-clang}

mkdir -p "$OUT"

FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-jump-tables \
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fdata-sections -fno-function-sections -Os -Wall -Wextra -Werror"
INC="-I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/module \
  -I$ROOT/manager/protocol -I$ROOT/manager/client -I$ROOT/manager/ui \
  -I$ROOT/app-sdk/ui \
  -I$PACK/probe/native-manager"

for src in \
    manager/protocol/canopus_protocol.c \
    manager/client/canopus_client.c \
    manager/ui/canopus_manager.c \
    manager/ui/canopus_manager_native.c \
    app-sdk/ui/canopus_ui.c \
    runtime/lifecycle/canopus_lifecycle.c \
    runtime/module/canopus_module.c \
    targets/$TARGET_ID/probe/native-manager/canopus_manager_native_probe.c; do
    base=$(basename "$src" .c)
    $CC $FLAGS $INC -c "$ROOT/$src" -o "$OUT/$base.o"
done

ld.lld -r -o "$OUT/canopus_manager_native_probe.ko" \
    "$OUT/canopus_protocol.o" \
    "$OUT/canopus_client.o" \
    "$OUT/canopus_manager.o" \
    "$OUT/canopus_manager_native.o" \
    "$OUT/canopus_ui.o" \
    "$OUT/canopus_lifecycle.o" \
    "$OUT/canopus_module.o" \
    "$OUT/canopus_manager_native_probe.o"

file "$OUT/canopus_manager_native_probe.ko"
"$ROOT/target/debug/canopus" verify \
    "$OUT/canopus_manager_native_probe.ko" \
    --target "$TARGET_ID" \
    --targets-dir "$ROOT/targets"
shasum -a 256 "$OUT/canopus_manager_native_probe.ko"
