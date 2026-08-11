#!/bin/sh
# Builds the Canopus supervisor and Manager backend for a selected target.
# Select with CANOPUS_TARGET; defaults to xiaomi-band-10-pro-3.101.030.
#
# Uses the real device platform (register /dev/canopus via the stock
# register_driver, exactly like btpatch registers /dev/btpatch) so the
# installer watchface's status/command surface works on device. The module is
# a zero-import ELF32 ET_REL and must PASS the Canopus verifier.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET_ID=${CANOPUS_TARGET:-xiaomi-band-10-pro-3.101.030}
LOADER_SRCS=""
LOADER_OBJECTS=""
case "$TARGET_ID" in
    xiaomi-band-10-pro-3.101.030|xiaomi-band-10-pro-3.101.036)
        MANAGER_BACKEND="manager/target/lvgl_v9/canopus_manager_target_lvgl_v9.c"
        ;;
    xiaomi-band-9-pro-3.1.175)
        MANAGER_BACKEND="manager/target/lvgl_v8/canopus_manager_target_lvgl_v8.c"
        LOADER_SRCS="runtime/loader/canopus_arm_reloc.c runtime/loader/canopus_elf32_loader.c"
        ;;
    *)
        echo "error: unsupported supervisor target: $TARGET_ID" >&2
        exit 2
        ;;
esac
BACKEND_OBJECT=$(basename "$MANAGER_BACKEND" .c).o

PACK_DIR="$ROOT/targets/$TARGET_ID"
GENERATED="$PACK_DIR/generated/canopus_veneer.h"
TARGET_CONFIG="$PACK_DIR/generated/canopus_target_config.h"
OUT="$ROOT/watchfaces/canopus-installer/build/$TARGET_ID"
if [ -n "$LOADER_SRCS" ]; then
    LOADER_OBJECTS="$OUT/canopus_arm_reloc.o $OUT/canopus_elf32_loader.o"
fi
# Stock modlib targets retain the proven 68 KiB budget. The Band-9 custom
# loader allocates its verified image from the default heap and has a separate
# 96 KiB ceiling covering the portable ELF loader itself.
case "$TARGET_ID" in
    xiaomi-band-9-pro-3.1.175) MAX_SIZE=98304 ;;
    *) MAX_SIZE=69632 ;;
esac
CC=${CC:-clang}

[ -f "$GENERATED" ] || {
    echo "error: run 'canopus target generate-veneer $TARGET_ID' first"
    exit 1
}
[ -f "$TARGET_CONFIG" ] || {
    echo "error: target lacks generated/canopus_target_config.h: $TARGET_ID"
    exit 1
}
if ! grep -q '^#define CANOPUS_SUP_PLATFORM_COMPLETE 1$' "$TARGET_CONFIG"; then
    echo "error: supervisor platform primitives are incomplete for $TARGET_ID" >&2
    exit 2
fi

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
  -I$ROOT/runtime/loader \
  -I$ROOT/manager/service -I$ROOT/manager/protocol -I$ROOT/manager/client \
  -I$ROOT/manager/ui -I$ROOT/manager/package -I$ROOT/manager/target \
  -I$ROOT/manager/native-app -I$ROOT/app-sdk/ui \
  -I$ROOT/third_party/monocypher -I$ROOT/third_party/sha256 \
  -I$PACK_DIR/generated"

# The v2 transport (CAN-P0-008) pulls the protocol codec and the snapshot
# helpers into the module; both are freestanding (no libc).
for s in \
    manager/service/canopus_supervisor.c \
    manager/service/canopus_supervisor_module.c \
    manager/service/canopus_supervisor_platform.c \
    manager/protocol/canopus_protocol.c \
    manager/client/canopus_client.c \
    manager/package/canopus_installer_bundle.c \
    manager/ui/canopus_manager.c \
    manager/ui/canopus_manager_native.c \
    app-sdk/ui/canopus_ui.c \
    third_party/sha256/sha256.c \
    "$MANAGER_BACKEND" \
    runtime/control/canopus_control.c \
    runtime/lifecycle/canopus_lifecycle.c \
    runtime/module/canopus_module.c \
    runtime/resources/canopus_resource.c \
    $LOADER_SRCS; do
    base=$(basename "$s")
    $CC $TARGET_FLAGS $INC -c "$ROOT/$s" -o "$OUT/${base%.c}.o"
done

# Monocypher is a general-purpose library, but the supervisor only needs its
# Ed25519 verifier. Compile it with per-function sections and retain the single
# public verification root so unrelated Argon2/X25519 code cannot introduce
# unused ARM runtime helpers or consume device flash.
$CC $TARGET_FLAGS -ffunction-sections $INC \
    -c "$ROOT/third_party/monocypher/monocypher.c" \
    -o "$OUT/monocypher-fs.o"
$CC $TARGET_FLAGS -ffunction-sections $INC \
    -c "$ROOT/third_party/monocypher/monocypher-ed25519.c" \
    -o "$OUT/monocypher-ed25519-fs.o"
$CC $TARGET_FLAGS $INC \
    -c "$ROOT/third_party/monocypher/canopus_monocypher_compat.c" \
    -o "$OUT/canopus_monocypher_compat.o"
ld.lld -r --gc-sections -u crypto_ed25519_check \
    -o "$OUT/monocypher-ed25519-min.o" \
    "$OUT/monocypher-fs.o" \
    "$OUT/monocypher-ed25519-fs.o" \
    "$OUT/canopus_monocypher_compat.o"

echo "[2/3] relocatable link (ld.lld -r)"
ld.lld -r -T "$ROOT/scripts/canopus_supervisor_sections.ld" \
    -o "$OUT/canopus_supervisor.elf" \
    "$OUT/canopus_supervisor.o" \
    "$OUT/canopus_supervisor_module.o" \
    "$OUT/canopus_supervisor_platform.o" \
    "$OUT/canopus_protocol.o" \
    "$OUT/canopus_client.o" \
    "$OUT/canopus_installer_bundle.o" \
    "$OUT/canopus_manager.o" \
    "$OUT/canopus_manager_native.o" \
    "$OUT/canopus_ui.o" \
    "$OUT/monocypher-ed25519-min.o" \
    "$OUT/sha256.o" \
    "$OUT/$BACKEND_OBJECT" \
    "$OUT/canopus_control.o" \
    "$OUT/canopus_lifecycle.o" \
    "$OUT/canopus_module.o" \
    "$OUT/canopus_resource.o" \
    $LOADER_OBJECTS

actual_size=$(wc -c < "$OUT/canopus_supervisor.elf")
[ "$actual_size" -le "$MAX_SIZE" ] || {
    echo "error: supervisor is $actual_size bytes; target loader limit is $MAX_SIZE"
    exit 1
}
echo "      module size: $actual_size / $MAX_SIZE bytes"

echo "[3/3] Canopus ELF verifier"
"$ROOT/target/debug/canopus" verify "$OUT/canopus_supervisor.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"

TARGET_STAGE="$ROOT/watchfaces/canopus-installer/canopus_supervisor-$TARGET_ID.bin"
INSTALLER_STAGE="$ROOT/watchfaces/canopus-installer/canopus_supervisor.bin"
cp "$OUT/canopus_supervisor.elf" "$TARGET_STAGE"
cp "$OUT/canopus_supervisor.elf" "$INSTALLER_STAGE"
echo "staged $(basename "$TARGET_STAGE")"
echo "staged canopus_supervisor.bin for $TARGET_ID"
if [ "$TARGET_ID" = xiaomi-band-9-pro-3.1.175 ]; then
    "$ROOT/scripts/build_band9_bootstrap.sh"
else
    rm -f "$ROOT/watchfaces/canopus-installer/canopus_supervisor-band9.bin" \
          "$ROOT/watchfaces/canopus-installer/canopus_supervisor-band9.elf" \
          "$ROOT/watchfaces/canopus-installer/canopus_stage2-band9.bin" \
          "$ROOT/watchfaces/canopus-installer/canopus_stage1_band9.lua"
fi
