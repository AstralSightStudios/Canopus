#!/bin/sh
# Builds the hello module for the xiaomi-band-10-pro-3.101.036 target.
#
# Produces a zero-import ELF32 ET_REL (Cortex-M33 Thumb soft-float) and runs
# the Canopus ELF verifier against the target pack. Flags match architecture
# §11.1; the authoritative set comes from the target loader profile.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TARGET_ID="xiaomi-band-10-pro-3.101.036"
PACK_DIR="$ROOT/targets/$TARGET_ID"
GENERATED="$PACK_DIR/generated/canopus_veneer.h"
OUT="$ROOT/modules/examples/hello/build"
CC=${CC:-clang}

[ -f "$GENERATED" ] || {
    echo "error: run 'canopus target generate-veneer $TARGET_ID' first"
    exit 1
}

mkdir -p "$OUT"
cd "$ROOT"

TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-stack-protector \
  -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections \
  -ffunction-sections -Os -Wall -Wextra -Werror -DCANOPUS_TARGET=1"

# -I on the generated directory so `#include "canopus_veneer.h"` resolves.
INC="-I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/resources \
  -I$ROOT/runtime/diagnostics -I$ROOT/runtime/control -I$ROOT/runtime/module \
  -I$ROOT/modules/examples/hello -I$PACK_DIR/generated"

SRCS="runtime/control/canopus_control.c runtime/lifecycle/canopus_lifecycle.c \
  runtime/resources/canopus_resource.c runtime/diagnostics/canopus_diagnostics.c \
  runtime/module/canopus_module.c modules/examples/hello/hello.c"

for s in $SRCS; do
    $CC $TARGET_FLAGS $INC -c "$ROOT/$s" -o "$OUT/$(basename "${s%.c}").o"
done

# relocatable link -> single ET_REL module. Use ld.lld (the macOS system ld
# cannot handle ARM ELF objects); lld infers the architecture from the inputs.
LD=${LD:-ld.lld}
$LD -r -o "$OUT/hello_module.elf" "$OUT"/canopus_control.o \
    "$OUT"/canopus_lifecycle.o "$OUT"/canopus_resource.o \
    "$OUT"/canopus_diagnostics.o "$OUT"/canopus_module.o "$OUT"/hello.o

file "$OUT/hello_module.elf"
echo "--- verifier ---"
"$ROOT/target/debug/canopus" verify "$OUT/hello_module.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"
