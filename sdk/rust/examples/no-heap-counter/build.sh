#!/bin/sh
# Builds the no-heap-counter Rust module for xiaomi-band-10-pro-3.101.036.
#
# Pipeline (architecture §12.3):
#   Rust no_std staticlib (panic=abort, feature=device)
#     + generated C constructor/destructor shim
#   -> ld.lld -r relocatable link
#   -> zero-import ELF32 ET_REL
#   -> Canopus ELF verifier against the target pack  (CAN-RUST-005, BLK-003)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET_ID="xiaomi-band-10-pro-3.101.036"
OUT="$SELF_DIR/build"
TRIPLE="thumbv8m.main-none-eabi"
CC=${CC:-clang}

mkdir -p "$OUT"

cd "$ROOT"

echo "[1/4] cargo build (no_std, panic=abort, device feature, staticlib)"
cargo build --profile device --target "$TRIPLE" -p no-heap-counter-device --features device

echo "[2/4] compile C constructor/destructor shim"
$CC --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
    -ffreestanding -fno-common -fno-builtin -fno-stack-protector \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -fdata-sections -ffunction-sections -Os -Wall -Wextra -Werror \
    -DCANOPUS_TARGET=1 -I "$ROOT/sdk/c" -I "$ROOT/targets/$TARGET_ID/generated" \
    -c "$ROOT/sdk/rust/c_shim/canopus_ctor.c" -o "$OUT/canopus_ctor.o"

echo "[3/4] relocatable link (ld.lld -r)"
# The shim references the descriptor and canopus_mod_stop, which pull in the
# members they need; module.ld drops debug info and embedded bitcode, which
# are not loaded and would make the Canopus ELF loader reject the image.
ld.lld -r -T "$ROOT/sdk/rust/c_shim/module.ld" \
    "$OUT/canopus_ctor.o" \
    "$ROOT/target/$TRIPLE/device/libno_heap_counter_device.a" \
    -o "$OUT/no_heap_counter.elf"

file "$OUT/no_heap_counter.elf"

echo "[4/4] Canopus ELF verifier"
"$ROOT/target/debug/canopus" verify "$OUT/no_heap_counter.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"
