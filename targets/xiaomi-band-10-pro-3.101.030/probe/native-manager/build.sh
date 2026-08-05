#!/bin/sh
# Build the resident exact-target native Manager/stock-LVX probe.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
TARGET_ID="xiaomi-band-10-pro-3.101.030"
SRC="$ROOT/targets/$TARGET_ID/probe/native-manager/canopus_manager_native_probe.c"
OUT="$ROOT/targets/$TARGET_ID/probe/native-manager/build"
CC=${CC:-clang}

mkdir -p "$OUT"

"$CC" \
    --target=arm-none-eabi \
    -mcpu=cortex-m33 \
    -mthumb \
    -mfloat-abi=soft \
    -ffreestanding \
    -fno-common \
    -fno-builtin \
    -fno-stack-protector \
    -fno-unwind-tables \
    -fno-asynchronous-unwind-tables \
    -fdata-sections \
    -ffunction-sections \
    -Os \
    -Wall \
    -Wextra \
    -Werror \
    -c "$SRC" \
    -o "$OUT/canopus_manager_native_probe.ko"

file "$OUT/canopus_manager_native_probe.ko"
"$ROOT/target/debug/canopus" verify \
    "$OUT/canopus_manager_native_probe.ko" \
    --target "$TARGET_ID" \
    --targets-dir "$ROOT/targets"
shasum -a 256 "$OUT/canopus_manager_native_probe.ko"
