#!/bin/sh
# Experimental exact-P65 stock-modlib resident Supervisor and native Manager.
# Does not stage production resources or require a hardware validation result.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
TARGET=xiaomi-p65-3.100.043
# Distinct recovered-runtime candidate; no system autostart claims.
OUT="$ROOT/build/p65-runtime"
CC=${CC:-clang}
LD=${LD:-ld.lld}
FIRMWARE=${CANOPUS_P65_FIRMWARE:?Set CANOPUS_P65_FIRMWARE to the exact P65 AP payload or OTA}
python3 - "$FIRMWARE" <<'PY'
import hashlib, pathlib, sys, zipfile
path = pathlib.Path(sys.argv[1])
expected = 'a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858'
if zipfile.is_zipfile(path):
    with zipfile.ZipFile(path) as archive:
        members = [member for member in archive.infolist()
                   if pathlib.PurePosixPath(member.filename).name == 'vela_ap.bin']
        if len(members) != 1 or members[0].file_size != 11793540:
            raise SystemExit('error: missing or ambiguous P65 AP payload')
        payload = archive.read(members[0])
else:
    payload = path.read_bytes()
if hashlib.sha256(payload).hexdigest() != expected:
    raise SystemExit('error: P65 AP SHA-256 mismatch')
PY
python3 "$ROOT/scripts/verify_p65_ui_flags.py" "$FIRMWARE"
mkdir -p "$OUT"
python3 "$ROOT/scripts/prepare_p65_runtime_key.py" "$OUT"
FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
-ffreestanding -fno-common -fno-builtin -fno-jump-tables -fno-stack-protector
-fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections
-ffunction-sections -Os -Wall -Wextra -Werror
-DCANOPUS_SUP_P65_MODLIB=1 -DCANOPUS_SUP_P65_RESIDENT=1 -DCANOPUS_P65_RECOVERED_RUNTIME=1"
INC="-I$OUT -Isdk/c -Iruntime/lifecycle -Iruntime/resources -Iruntime/diagnostics
-Iruntime/control -Iruntime/module -Imanager/service -Imanager/protocol
-Imanager/client -Imanager/ui -Imanager/package -Imanager/target
-Imanager/target/p65 -Iapp-sdk/ui -Ithird_party/monocypher -Ithird_party/sha256
-Itargets/$TARGET/generated"
OBJECTS=""
for source in \
    manager/service/canopus_supervisor.c \
    manager/service/canopus_supervisor_module.c \
    manager/service/canopus_supervisor_platform.c \
    manager/protocol/canopus_protocol.c manager/client/canopus_client.c \
    manager/package/canopus_installer_bundle.c \
    manager/ui/canopus_manager.c manager/ui/canopus_manager_native.c \
    app-sdk/ui/canopus_ui.c third_party/sha256/sha256.c \
    manager/target/p65/canopus_manager_target_p65.c \
    manager/target/p65/canopus_manager_p65_backend.c \
    manager/target/p65/canopus_manager_p65_transport.c \
    manager/target/p65/canopus_manager_p65_rows.c \
    manager/target/p65/canopus_manager_p65_lvx.c \
    manager/target/p65/canopus_manager_p65_registry.c \
    manager/target/p65/canopus_p65_registry_firmware.c \
    runtime/control/canopus_control.c runtime/lifecycle/canopus_lifecycle.c \
    runtime/module/canopus_module.c runtime/resources/canopus_resource.c; do
    object="$OUT/$(basename "$source" .c).o"
    $CC $FLAGS $INC -c "$source" -o "$object"
    OBJECTS="$OBJECTS $object"
done
for source in monocypher monocypher-ed25519; do
    $CC $FLAGS -ffunction-sections $INC -c "third_party/monocypher/$source.c" \
        -o "$OUT/$source-fs.o"
done
$CC $FLAGS $INC -c third_party/monocypher/canopus_monocypher_compat.c \
    -o "$OUT/canopus_monocypher_compat.o"
$LD -r --gc-sections -u crypto_ed25519_check -u __aeabi_memcpy -o "$OUT/crypto-min.o" \
    "$OUT/monocypher-fs.o" "$OUT/monocypher-ed25519-fs.o" \
    "$OUT/canopus_monocypher_compat.o"
$LD -r --gc-sections -T scripts/canopus_module_sections.ld \
    -e canopus_supervisor_module_initialize \
    -o "$OUT/canopus_supervisor.elf" $OBJECTS "$OUT/crypto-min.o"
if command -v cargo >/dev/null 2>&1; then cargo build --locked -p canopus-cli; fi
[ -x "$ROOT/target/debug/canopus" ] || { printf "Build Canopus CLI with cargo first\n" >&2; exit 1; }
# Keep experimental address permissions out of the production/SDK pack.
# Overlay keeps the exact target identity, ranges and relocation constraints;
# only explicitly listed exact-P65 test ABI addresses are added.
python3 "$ROOT/scripts/generate_p65_experimental_pack.py" --output "$OUT/targets"
"$ROOT/target/debug/canopus" verify "$OUT/canopus_supervisor.elf" \
    --target "$TARGET" --targets-dir "$OUT/targets"
printf 'Built recovered P65 runtime candidate: %s\n' "$OUT/canopus_supervisor.elf"
printf 'System autostart is not implemented. The old deployment remains disabled.\n'
