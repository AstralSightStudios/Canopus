#!/bin/sh
# Builds the Canopus supervisor and Manager backend for a selected target.
# Select with CANOPUS_TARGET; defaults to the trusted 3.101.036 target.
#
# Uses the real device platform (register /dev/canopus via the stock
# register_driver, exactly like btpatch registers /dev/btpatch) so the
# installer watchface's status/command surface works on device. The module is
# a zero-import ELF32 ET_REL and must PASS the Canopus verifier.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET_ID=${CANOPUS_TARGET:-xiaomi-band-10-pro-3.101.036}
LOADER_OBJECTS=""
CANOPUS_BIN="$ROOT/target/debug/canopus"
[ -x "$CANOPUS_BIN" ] || (cd "$ROOT" && cargo build -q -p canopus-cli)
# Family build data (manager backend, size limit, loader sources, defines).
TARGET_INFO=$("$CANOPUS_BIN" target info "$TARGET_ID" --targets-dir "$ROOT/targets" --shell) || {
    echo "error: unsupported supervisor target: $TARGET_ID" >&2
    exit 2
}
eval "$TARGET_INFO"
BACKEND_OBJECT=$(basename "$MANAGER_BACKEND" .c).o

PACK_DIR="$ROOT/targets/$TARGET_ID"
GENERATED="$PACK_DIR/generated/canopus_veneer.h"
TARGET_CONFIG="$PACK_DIR/generated/canopus_target_config.h"
OUT="$ROOT/watchfaces/canopus-installer/build/$TARGET_ID"
if [ -n "$LOADER_SRCS" ]; then
    LOADER_OBJECTS="$OUT/canopus_arm_reloc.o $OUT/canopus_elf32_loader.o"
fi
# Stock modlib has no fixed insertion ceiling. Band 11 carries the portable
# child-module loader because its firmware has no approved insmod command.
CC=${CC:-clang}

[ -f "$GENERATED" ] || {
    echo "error: run 'canopus target generate $TARGET_ID' first"
    exit 1
}
[ -f "$TARGET_CONFIG" ] || {
    echo "error: target lacks generated/canopus_target_config.h: $TARGET_ID"
    exit 1
}
if [ "$PROD_FAMILY" != xiaomi-band-11 ] && ! grep -q '^#define CANOPUS_SUP_PLATFORM_COMPLETE 1$' "$TARGET_CONFIG"; then
    echo "error: supervisor platform ABI is not production-complete for $TARGET_ID" >&2
    exit 2
fi

mkdir -p "$OUT"
cd "$ROOT"
if [ "$PROD_FAMILY" = xiaomi-band-11 ]; then
    "$CANOPUS_BIN" target generate "$TARGET_ID" --targets-dir "$ROOT/targets"
fi

echo "[1/3] compile supervisor (Cortex-M33 Thumb soft-float)"
# Flags mirror native/scripts/build_btpatch_phase5.sh; -fno-function-sections
# is btpatch's proven configuration for a boot-resident constructor module.
# Backend selection follows the exact target configuration, not a user opt-in.
python3 "$ROOT/scripts/generate_l2_probe_config.py" "$TARGET_ID" "$OUT/canopus_l2_probe_config.h"
TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-jump-tables \
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fdata-sections -fno-function-sections -Os -Wall -Wextra -Werror \
  $TARGET_DEFINES ${CANOPUS_SUP_EXTRA_DEFINES:-}"

INC="-I$OUT -I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/resources \
  -I$ROOT/runtime/diagnostics -I$ROOT/runtime/control -I$ROOT/runtime/module \
  -I$ROOT/runtime/loader \
  -I$ROOT/manager/service -I$ROOT/manager/protocol -I$ROOT/manager/client \
  -I$ROOT/manager/ui -I$ROOT/manager/package -I$ROOT/manager/target \
  -I$ROOT/manager/native-app -I$ROOT/app-sdk/ui \
  -I$ROOT/third_party/sha256 \
  -I$PACK_DIR/generated"

# The v2 transport (CAN-P0-008) pulls the protocol codec and the snapshot
# helpers into the module; both are freestanding (no libc).
for s in \
    manager/service/canopus_supervisor.c \
    manager/service/canopus_supervisor_services.c \
    manager/service/canopus_supervisor_upload.c \
    manager/service/canopus_supervisor_l2.c \
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
    runtime/hook/canopus_hook.c \
    $LOADER_SRCS; do
    base=$(basename "$s")
    $CC $TARGET_FLAGS $INC -c "$ROOT/$s" -o "$OUT/${base%.c}.o"
done

echo "[2/3] relocatable link (ld.lld -r)"
ld.lld -r -T "$ROOT/scripts/canopus_supervisor_sections.ld" \
    -o "$OUT/canopus_supervisor.elf" \
    "$OUT/canopus_supervisor.o" \
    "$OUT/canopus_supervisor_services.o" \
    "$OUT/canopus_supervisor_upload.o" \
    "$OUT/canopus_supervisor_l2.o" \
    "$OUT/canopus_supervisor_module.o" \
    "$OUT/canopus_supervisor_platform.o" \
    "$OUT/canopus_protocol.o" \
    "$OUT/canopus_client.o" \
    "$OUT/canopus_installer_bundle.o" \
    "$OUT/canopus_manager.o" \
    "$OUT/canopus_manager_native.o" \
    "$OUT/canopus_ui.o" \
    "$OUT/sha256.o" \
    "$OUT/$BACKEND_OBJECT" \
    "$OUT/canopus_control.o" \
    "$OUT/canopus_lifecycle.o" \
    "$OUT/canopus_module.o" \
    "$OUT/canopus_resource.o" \
    "$OUT/canopus_hook.o" \
    $LOADER_OBJECTS

# Drop unreferenced debug/local symbol metadata on stock-modlib targets. Keep
# all relocation targets, global exports and constructors; runtime sections
# remain byte-identical. This keeps the additional installer endpoint within
# the existing 72 KiB package budget without increasing that budget.
if [ "$PROD_FAMILY" = xiaomi-band-10-pro ]; then
    OBJCOPY=${OBJCOPY:-${RUST_OBJCOPY:-}}
    if [ -z "$OBJCOPY" ]; then
        if command -v llvm-objcopy >/dev/null 2>&1; then
            OBJCOPY="llvm-objcopy"
        elif [ -x "/opt/homebrew/opt/llvm/bin/llvm-objcopy" ]; then
            OBJCOPY="/opt/homebrew/opt/llvm/bin/llvm-objcopy"
        elif [ -x "/usr/local/opt/llvm/bin/llvm-objcopy" ]; then
            OBJCOPY="/usr/local/opt/llvm/bin/llvm-objcopy"
        elif command -v arm-none-eabi-objcopy >/dev/null 2>&1; then
            OBJCOPY="arm-none-eabi-objcopy"
        elif command -v rust-objcopy >/dev/null 2>&1; then
            OBJCOPY="rust-objcopy"
        else
            OBJCOPY="$(rustc --print target-libdir 2>/dev/null)/../bin/rust-objcopy"
        fi
    fi
    RUST_SYSROOT=$(rustc --print sysroot 2>/dev/null || true)
    env DYLD_FALLBACK_LIBRARY_PATH="${RUST_SYSROOT}/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}" \
        LD_LIBRARY_PATH="${RUST_SYSROOT}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$OBJCOPY" --strip-debug --strip-unneeded "$OUT/canopus_supervisor.elf" \
            "$OUT/canopus_supervisor.stripped.elf"
    mv "$OUT/canopus_supervisor.stripped.elf" "$OUT/canopus_supervisor.elf"
fi

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
PROD_FAMILY_STAGE="$ROOT/watchfaces/canopus-installer-prod/$PROD_FAMILY"
MANAGER_ICON_SOURCE="$ROOT/watchfaces/canopus-installer/manager_icon.bin"
[ -f "$MANAGER_ICON_SOURCE" ] || {
    echo "error: missing production Manager icon: $MANAGER_ICON_SOURCE" >&2
    exit 1
}
mkdir -p "$PROD_FAMILY_STAGE"
cp "$MANAGER_ICON_SOURCE" "$PROD_FAMILY_STAGE/manager_icon.bin"
cp "$OUT/canopus_supervisor.elf" "$TARGET_STAGE"
if [ "$PROD_FAMILY" = xiaomi-band-11 ]; then
    python3 "$ROOT/scripts/build_band11_installer.py" --target "$TARGET_ID" --supervisor "$OUT/canopus_supervisor.elf" \
        --output-dir "${CANOPUS_BAND11_OUTPUT_DIR:-$PROD_FAMILY_STAGE}"
else
    mkdir -p "$PROD_FAMILY_STAGE"
    PROD_TARGET_STAGE="$PROD_FAMILY_STAGE/canopus_supervisor-$TARGET_ID.bin"
    cp "$OUT/canopus_supervisor.elf" "$PROD_TARGET_STAGE"
    echo "staged canopus-installer-prod/$PROD_FAMILY/$(basename "$PROD_TARGET_STAGE")"
fi
rm -f "$ROOT/watchfaces/canopus-installer/canopus_supervisor.bin" \
      "$ROOT/watchfaces/canopus-installer-prod/canopus_supervisor.bin"
