#!/bin/sh
# Build the exact-target NSH/mw stage-1/stage-2 bootstrap resources.
# The target-local loader profile is the only source of firmware addresses.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODE=stage
if [ "${1:-}" = "--host-check" ]; then
    MODE=host-check
    shift
fi
TARGET_ID=${1:-xiaomi-band-9-pro-3.1.175}
if [ "$MODE" = host-check ]; then
    SUPERVISOR=${2:?usage: build_band9_bootstrap.sh --host-check target-id exact-target-et-rel [output-dir]}
    OUT=${3:-"/tmp/canopus-band9-host-check-$TARGET_ID"}
    TARGET_STAGE=
else
    OUT=${2:-"$ROOT/watchfaces/canopus-installer/build/$TARGET_ID"}
    SUPERVISOR="$OUT/canopus_supervisor.elf"
    STAGE_ROOT=${CANOPUS_INSTALLER_STAGE_ROOT:-"$ROOT/watchfaces/canopus-installer"}
    TARGET_STAGE="$STAGE_ROOT/targets/$TARGET_ID"
fi
PROFILE="$ROOT/targets/$TARGET_ID/loader/bootstrap.toml"
TARGET_TOML="$ROOT/targets/$TARGET_ID/target.toml"
CONFIG="$OUT/canopus_band9_loader_config.h"
CC=${CC:-clang}
CANOPUS=${CANOPUS_CLI:-"$ROOT/target/debug/canopus"}

[ "$TARGET_ID" = "$(basename "$TARGET_ID")" ] || {
    echo "error: invalid target id: $TARGET_ID" >&2
    exit 1
}
[ -f "$PROFILE" ] || {
    echo "error: target has no loader profile: $TARGET_ID" >&2
    exit 1
}
[ -f "$TARGET_TOML" ] || {
    echo "error: target has no target.toml: $TARGET_ID" >&2
    exit 1
}

mkdir -p "$OUT"
python3 "$ROOT/scripts/generate_band9_loader_config.py" \
    --profile "$PROFILE" --target-toml "$TARGET_TOML" \
    --header "$CONFIG"

[ -f "$SUPERVISOR" ] || {
    echo "error: build the exact-target supervisor first: $SUPERVISOR" >&2
    exit 1
}
[ -x "$CANOPUS" ] || {
    echo "error: Canopus CLI not found: $CANOPUS" >&2
    exit 1
}
"$CANOPUS" verify "$SUPERVISOR" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"

if [ "$MODE" = stage ]; then
    mkdir -p "$TARGET_STAGE"
    PROFILE_LUA="$TARGET_STAGE/canopus_loader_profile.lua"
else
    PROFILE_LUA="$OUT/canopus_loader_profile.lua"
fi
python3 "$ROOT/scripts/generate_band9_loader_config.py" \
    --profile "$PROFILE" --target-toml "$TARGET_TOML" \
    --lua "$PROFILE_LUA"

SIZE=$(wc -c < "$SUPERVISOR" | tr -d ' ')
FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft"
COMMON="-ffreestanding -fPIC -fno-common -fno-builtin -fno-jump-tables -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections -ffunction-sections -Os -Wall -Wextra -Werror"
INCLUDES="-I$ROOT/sdk/c -I$ROOT/runtime/loader -I$OUT"

# The profile generator refuses PENDING or incomplete profiles before any
# target address is compiled.
echo "[1/4] compile target-local stage 2 for $TARGET_ID"
$CC $FLAGS $COMMON $INCLUDES \
    -DCANOPUS_BAND9_SUPERVISOR_SIZE="$SIZE" \
    -c "$ROOT/manager/target/band9/canopus_band9_stage2.c" \
    -o "$OUT/stage2.o"
$CC $FLAGS $COMMON $INCLUDES \
    -c "$ROOT/runtime/loader/canopus_arm_reloc.c" \
    -o "$OUT/stage2-reloc.o"
$CC $FLAGS $COMMON $INCLUDES \
    -c "$ROOT/runtime/loader/canopus_elf32_loader.c" \
    -o "$OUT/stage2-elf.o"
ld.lld -static --gc-sections --oformat=binary \
    -T "$ROOT/scripts/canopus_band9_stage2.ld" -o "$OUT/stage2.bin" \
    "$OUT/stage2.o" "$OUT/stage2-reloc.o" "$OUT/stage2-elf.o"

STAGE2_SIZE=$(wc -c < "$OUT/stage2.bin" | tr -d ' ')
echo "[2/4] compile target-local stage 1"
$CC $FLAGS $COMMON $INCLUDES \
    -DCANOPUS_BAND9_STAGE2_SIZE="$STAGE2_SIZE" \
    -c "$ROOT/manager/target/band9/canopus_band9_stage1.c" \
    -o "$OUT/stage1.o"
ld.lld -static --gc-sections --oformat=binary \
    -T "$ROOT/scripts/canopus_band9_stage1.ld" -o "$OUT/stage1.bin" \
    "$OUT/stage1.o"

# Compile the mailbox veneer as a configuration check. The stage binaries use
# Lua mw writes for this veneer, so it is not linked into stage 1 or stage 2.
CAVE_RESULT=$(python3 - "$PROFILE" <<'PY'
import pathlib, sys, tomllib
with pathlib.Path(sys.argv[1]).open("rb") as stream:
    profile = tomllib.load(stream)
print(profile["sram_text"]["result_word"])
PY
)
echo "[3/4] compile target-local mailbox veneer"
$CC $FLAGS $COMMON -DCANOPUS_BAND9_CAVE_RESULT="$CAVE_RESULT" \
    -c "$ROOT/manager/target/band9/canopus_band9_cave.S" \
    -o "$OUT/canopus_band9_cave.o"

if [ "$MODE" = stage ]; then
    python3 "$ROOT/tools/band9-stage1-lua.py" "$OUT/stage1.bin" \
        "$TARGET_STAGE/canopus_stage1.lua"
    cp "$OUT/stage2.bin" "$TARGET_STAGE/canopus_stage2.bin"
    cp "$SUPERVISOR" "$TARGET_STAGE/canopus_supervisor.bin"
    echo "[4/4] staged target-local bootstrap"
else
    python3 "$ROOT/tools/band9-stage1-lua.py" "$OUT/stage1.bin" \
        "$OUT/canopus_stage1.lua"
    echo "[4/4] host-check artifacts retained in $OUT; nothing staged"
fi
echo "target=$TARGET_ID stage1=$(wc -c < "$OUT/stage1.bin" | tr -d ' ') bytes stage2=$STAGE2_SIZE bytes et_rel=$SIZE bytes"
