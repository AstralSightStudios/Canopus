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
# Staged Kmem loader targets (Band 9 family): materialize the exact inode /
# MPU / allocator addresses the platform and stage1/stage2 compile against.
if [ -f "$PACK_DIR/loader/bootstrap.toml" ]; then
    python3 "$ROOT/scripts/generate_band9_loader_config.py" \
        --profile "$PACK_DIR/loader/bootstrap.toml" \
        --target-toml "$PACK_DIR/target.toml" \
        --header "$OUT/canopus_band9_loader_config.h"
fi

echo "[1/3] compile supervisor (Cortex-M33 Thumb soft-float)"
# Flags mirror native/scripts/build_btpatch_phase5.sh; -fno-function-sections
# is btpatch's proven configuration for a boot-resident constructor module.
# Backend selection follows the exact target configuration, not a user opt-in.
python3 "$ROOT/scripts/generate_l2_probe_config.py" "$TARGET_ID" "$OUT/canopus_l2_probe_config.h"
python3 "$ROOT/scripts/generate_boot_config.py" "$TARGET_ID" "$OUT/canopus_boot_config.h"
TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-jump-tables \
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fdata-sections -fno-function-sections -Os -Wall -Wextra -Werror \
  $TARGET_DEFINES ${CANOPUS_SUP_EXTRA_DEFINES:-}"
# The v8 Manager app descriptor layout differs between the 3.1.32 generation
# and 3.1.190: the latter's launcher_load_app_info reads the metadata callback
# at +0x1c. Select the 3.1.190 layout for targets whose IDB proved it.
case "$TARGET_ID" in
    xiaomi-band-9-pro-3.1.190)
        TARGET_FLAGS="$TARGET_FLAGS -DCANOPUS_MGR_APPINFO_190 -DCANOPUS_MGR_SCREEN_WIDTH=336 -DCANOPUS_MGR_TITLE_USER_DATA -DCANOPUS_MGR_PAGE_ROOT_SLOT=0x2c -DCANOPUS_MGR_TEXT_FONT_PROP=0x57" ;;
    xiaomi-band-10-3.1.28)
        TARGET_FLAGS="$TARGET_FLAGS -DCANOPUS_MGR_APPINFO_1028 -DCANOPUS_MGR_SCREEN_WIDTH=212" ;;
    xiaomi-band-9-3.1.32)
        TARGET_FLAGS="$TARGET_FLAGS -DCANOPUS_MGR_SCREEN_WIDTH=192 -DCANOPUS_MGR_TITLE_PLAIN -DCANOPUS_MGR_PAGE_ROOT_SLOT=0x2c -DCANOPUS_MGR_TEXT_FONT_PROP=0x57" ;;
esac

INC="-I$OUT -I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/resources \
  -I$ROOT/runtime/diagnostics -I$ROOT/runtime/control -I$ROOT/runtime/module \
  -I$ROOT/runtime/loader \
  -I$ROOT/manager/service -I$ROOT/manager/protocol -I$ROOT/manager/client \
  -I$ROOT/manager/ui -I$ROOT/manager/package -I$ROOT/manager/target \
  -I$ROOT/manager/native-app \
  -I$ROOT/third_party/sha256 \
  -I$PACK_DIR/generated"

# The LVGL v8 family implements the Manager directly in its backend; the
# shared v9 UI host does not apply. Everything else still links the runtime.
UI_HOST_SRC="manager/target/canopus_ui_host.c"
UI_HOST_OBJECT="$OUT/canopus_ui_host.o"
IS_STAGED_V8=0
IS_STAGED_LOADER=0
case "$PROD_FAMILY" in
    xiaomi-band-9|xiaomi-band-9-pro|xiaomi-band-10) IS_STAGED_V8=1 ;;
esac
# Families whose installer ships the NSH-mw stage1/stage2 chain. The REDMI
# Watch 6 uses the same loader but the v9 UI host, so it is staged here
# without selecting the v8 backend.
case "$PROD_FAMILY" in
    xiaomi-band-9|xiaomi-band-9-pro|xiaomi-band-10|redmi-watch) IS_STAGED_LOADER=1 ;;
esac
if [ "$IS_STAGED_V8" = 1 ]; then
    UI_HOST_SRC=""
    UI_HOST_OBJECT=""
fi

# The v2 transport (CAN-P0-008) pulls the protocol codec and the snapshot
# helpers into the module; both are freestanding (no libc).
for s in \
    manager/service/canopus_supervisor.c \
    manager/service/canopus_supervisor_boot.c \
    manager/service/canopus_supervisor_services.c \
    manager/service/canopus_supervisor_upload.c \
    manager/service/canopus_supervisor_connect.c \
    manager/service/canopus_supervisor_l2.c \
    manager/service/canopus_supervisor_module.c \
    manager/service/canopus_supervisor_platform.c \
    manager/protocol/canopus_protocol.c \
    manager/client/canopus_client.c \
    manager/package/canopus_installer_bundle.c \
    manager/ui/canopus_manager.c \
    manager/ui/canopus_manager_native.c \
    runtime/ui/canopus_ui.c \
    sdk/c/canopus_aeabi_mem.c \
    third_party/sha256/sha256.c \
    $UI_HOST_SRC \
    "$MANAGER_BACKEND" \
    runtime/control/canopus_control.c \
    runtime/lifecycle/canopus_lifecycle.c \
    runtime/module/canopus_module.c \
    runtime/resources/canopus_resource.c \
    runtime/hook/canopus_hook.c \
    $LOADER_SRCS; do
    base=$(basename "$s")
    # This backend owns layout and native fit arithmetic; prefer minimum code
    # size within the unchanged Supervisor file/resident budgets.
    SOURCE_FLAGS=""
    [ "$s" != manager/target/canopus_ui_host.c ] || SOURCE_FLAGS="-Oz"
    # Likewise the L2 transport and owner pump: the deep request ring would otherwise take
    # .155 past its file budget.
    [ "$s" != manager/service/canopus_supervisor_l2.c ] || SOURCE_FLAGS="-Oz"
    # And the staging/upload path (not a hot one): with package resources (CPC2 26-28) the .043
    # Supervisor no longer fits its resident budget otherwise.
    [ "$s" != manager/service/canopus_supervisor_upload.c ] || SOURCE_FLAGS="-Oz"
    $CC $TARGET_FLAGS $SOURCE_FLAGS $INC -c "$ROOT/$s" -o "$OUT/${base%.c}.o"
done

echo "[2/3] relocatable link (ld.lld -r)"
ld.lld -r -T "$ROOT/scripts/canopus_supervisor_sections.ld" \
    -o "$OUT/canopus_supervisor.elf" \
    "$OUT/canopus_supervisor.o" \
    "$OUT/canopus_supervisor_boot.o" \
    "$OUT/canopus_supervisor_services.o" \
    "$OUT/canopus_supervisor_upload.o" \
    "$OUT/canopus_supervisor_connect.o" \
    "$OUT/canopus_supervisor_l2.o" \
    "$OUT/canopus_supervisor_module.o" \
    "$OUT/canopus_supervisor_platform.o" \
    "$OUT/canopus_protocol.o" \
    "$OUT/canopus_client.o" \
    "$OUT/canopus_installer_bundle.o" \
    "$OUT/canopus_manager.o" \
    "$OUT/canopus_manager_native.o" \
    "$OUT/canopus_ui.o" \
    "$OUT/canopus_aeabi_mem.o" \
    "$OUT/sha256.o" \
    $UI_HOST_OBJECT \
    "$OUT/$BACKEND_OBJECT" \
    "$OUT/canopus_control.o" \
    "$OUT/canopus_lifecycle.o" \
    "$OUT/canopus_module.o" \
    "$OUT/canopus_resource.o" \
    "$OUT/canopus_hook.o" \
    $LOADER_OBJECTS

# Both loaders retain relocation targets, global exports and constructors, but
# do not consume debug/local metadata or C unwind tables. Strip only unreferenced
# symbols; --strip-all would remove symbol tables the ELF loaders require.
if [ "$PROD_FAMILY" = xiaomi-band-10-pro ] || [ "$PROD_FAMILY" = xiaomi-band-11 ]; then
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
        "$OBJCOPY" --strip-debug --strip-unneeded \
            --remove-section=.ARM.exidx --remove-section=.rel.ARM.exidx \
            "$OUT/canopus_supervisor.elf" \
            "$OUT/canopus_supervisor.stripped.elf"
    mv "$OUT/canopus_supervisor.stripped.elf" "$OUT/canopus_supervisor.elf"
fi

actual_size=$(wc -c < "$OUT/canopus_supervisor.elf")
[ "$actual_size" -le "$MAX_SIZE" ] || {
    echo "error: supervisor is $actual_size bytes; target build budget is $MAX_SIZE"
    exit 1
}
echo "      module size: $actual_size / $MAX_SIZE bytes"

echo "[3/3] Canopus ELF verifier"
"$ROOT/target/debug/canopus" verify "$OUT/canopus_supervisor.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets" --max-resident "$MAX_RESIDENT"

TARGET_STAGE="$ROOT/watchfaces/canopus-installer/canopus_supervisor-$TARGET_ID.bin"
PROD_FAMILY_STAGE="$ROOT/watchfaces/canopus-installer-prod/$PROD_FAMILY"
MANAGER_ICON_SOURCE="$ROOT/watchfaces/canopus-installer/manager_icon.bin"
[ -f "$MANAGER_ICON_SOURCE" ] || {
    echo "error: missing production Manager icon: $MANAGER_ICON_SOURCE" >&2
    exit 1
}
mkdir -p "$PROD_FAMILY_STAGE"
if [ "$TARGET_ID" = xiaomi-band-9-pro-3.1.190 ]; then
    # The v8 firmware generation reads an lv_img_header_t, not the v9 header.
    python3 "$ROOT/scripts/convert_manager_icon.py" "$MANAGER_ICON_SOURCE" "$PROD_FAMILY_STAGE/manager_icon.bin"
elif [ "$TARGET_ID" = xiaomi-band-9-3.1.32 ]; then
    python3 "$ROOT/scripts/convert_manager_icon.py" --rgb565 "$MANAGER_ICON_SOURCE" "$PROD_FAMILY_STAGE/manager_icon.bin"
else
    cp "$MANAGER_ICON_SOURCE" "$PROD_FAMILY_STAGE/manager_icon.bin"
fi
cp "$OUT/canopus_supervisor.elf" "$TARGET_STAGE"
if [ "$PROD_FAMILY" = xiaomi-band-11 ]; then
    python3 "$ROOT/scripts/build_band11_installer.py" --target "$TARGET_ID" --supervisor "$OUT/canopus_supervisor.elf" \
        --output-dir "${CANOPUS_BAND11_OUTPUT_DIR:-$PROD_FAMILY_STAGE}"
elif [ "$IS_STAGED_LOADER" = 1 ]; then
    CANOPUS_INSTALLER_STAGE_ROOT="$PROD_FAMILY_STAGE" \
        sh "$ROOT/scripts/build_band9_bootstrap.sh" "$TARGET_ID" "$OUT"
    echo "staged canopus-installer-prod/$PROD_FAMILY/ (main.lua + stage1/stage2/profile/supervisor)"
else
    mkdir -p "$PROD_FAMILY_STAGE"
    PROD_TARGET_STAGE="$PROD_FAMILY_STAGE/canopus_supervisor-$TARGET_ID.bin"
    cp "$OUT/canopus_supervisor.elf" "$PROD_TARGET_STAGE"
    python3 "$ROOT/scripts/generate_boot_config.py" "$TARGET_ID" "$PROD_FAMILY_STAGE/canopus_boot-$TARGET_ID.bin" --payload
    echo "staged canopus-installer-prod/$PROD_FAMILY/$(basename "$PROD_TARGET_STAGE")"
fi
rm -f "$ROOT/watchfaces/canopus-installer/canopus_supervisor.bin" \
      "$ROOT/watchfaces/canopus-installer-prod/canopus_supervisor.bin"
