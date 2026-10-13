#!/bin/sh
# Builds the portable hello module once and verifies it has no firmware address
# of any registered target (`canopus verify --portable`).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="$ROOT/modules/examples/hello-portable/build"
CC=${CC:-clang}
LD=${LD:-ld.lld}

mkdir -p "$OUT"
cd "$ROOT"
$CC --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
    -ffreestanding -fno-common -fno-builtin -fno-stack-protector \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections \
    -ffunction-sections -Os -Wall -Wextra -Werror \
    -I"$ROOT/sdk/c" -c modules/examples/hello-portable/hello_portable.c \
    -o "$OUT/hello_portable.o"
$LD -r -o "$OUT/hello_portable.elf" "$OUT/hello_portable.o"

[ -x "$ROOT/target/debug/canopus" ] || (cd "$ROOT" && cargo build -q -p canopus-cli)
"$ROOT/target/debug/canopus" verify "$OUT/hello_portable.elf" --portable \
    --targets-dir "$ROOT/targets"
