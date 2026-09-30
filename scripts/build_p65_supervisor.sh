#!/bin/sh
# Experimental exact-P65 stock-modlib resident Supervisor and native Manager.
# Does not stage production resources or require a hardware validation result.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
TARGET=xiaomi-p65-3.100.043
# The only allowed build while withdrawn is a non-installable audit ELF.
AUDIT=0
case "${1:-}" in
    --offline-audit)
        AUDIT=1
        OUT="$ROOT/build/p65-offline-audit"
        ;;
    "") OUT=${CANOPUS_P65_OUTPUT_DIR:-"$ROOT/watchfaces/canopus-installer/build/$TARGET"} ;;
    *) printf 'Usage: %s [--offline-audit]\n' "$0" >&2; exit 2 ;;
esac
python3 - "$ROOT/manager/target/p65/canopus_p65_safety.h" "$AUDIT" <<'PY'
import pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text()
match = re.search(r'^#define CANOPUS_P65_DEPLOYMENT_ENABLED ([01])$', text, re.M)
if match is None:
    raise SystemExit('error: missing or invalid P65 safety policy')
if sys.argv[2] != '1' and match[1] != '1':
    raise SystemExit('error: P65 deployment withdrawn after Logo boot loop; do not install or retry')
if sys.argv[2] == '1' and match[1] != '0':
    raise SystemExit('error: offline audit requires the native deployment guard disabled')
PY
CC=${CC:-clang}
LD=${LD:-ld.lld}
FIRMWARE=${CANOPUS_P65_FIRMWARE:-"$HOME/develop/temp/p65_v3.100.043_vela_ap.bin"}
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
mkdir -p "$OUT"
FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
-ffreestanding -fno-common -fno-builtin -fno-jump-tables -fno-stack-protector
-fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections
-fno-function-sections -Os -Wall -Wextra -Werror
-DCANOPUS_SUP_P65_MODLIB=1 -DCANOPUS_SUP_P65_RESIDENT=1"
INC="-Isdk/c -Iruntime/lifecycle -Iruntime/resources -Iruntime/diagnostics
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
$LD -r -T scripts/canopus_module_sections.ld \
    -e canopus_supervisor_module_initialize \
    -o "$OUT/canopus_supervisor.elf" $OBJECTS "$OUT/crypto-min.o"
cargo build --locked -p canopus-cli
# Keep experimental address permissions out of the production/SDK pack.
# Overlay keeps the exact target identity, ranges and relocation constraints;
# only explicitly listed exact-P65 test ABI addresses are added.
python3 "$ROOT/scripts/generate_p65_experimental_pack.py" --output "$OUT/targets"
"$ROOT/target/debug/canopus" verify "$OUT/canopus_supervisor.elf" \
    --target "$TARGET" --targets-dir "$OUT/targets"
if [ "$AUDIT" = 1 ]; then
    printf '\nOffline-only P65 ELF: %s\n' "$OUT/canopus_supervisor.elf"
    printf 'Native entry is disabled. No installer resources generated. DO NOT INSTALL.\n'
else
    python3 "$ROOT/scripts/build_p65_installer.py" \
        --supervisor "$OUT/canopus_supervisor.elf" --targets "$OUT/targets" \
        --output "$OUT/installer"
fi
