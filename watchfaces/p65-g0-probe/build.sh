#!/bin/sh
# Build a zero-import P65 ET_REL probe and flat watchface resource bundle.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
FACE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET_ID=xiaomi-p65-3.100.043
OUT="$FACE_DIR/build"
CC=${CC:-clang}
LD=${LD:-ld.lld}
CLI="$ROOT/target/debug/canopus"

if [ ! -x "$CLI" ]; then
    (cd "$ROOT" && cargo build -p canopus-cli --bin canopus)
fi
mkdir -p "$OUT"

TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-stack-protector \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fdata-sections -ffunction-sections -Os -Wall -Wextra -Werror"

$CC $TARGET_FLAGS -c "$FACE_DIR/src/p65_g0_probe.c" \
    -o "$OUT/p65_g0_probe.o"
$LD -r -e canopus_module_initialize \
    -T "$ROOT/scripts/canopus_module_sections.ld" \
    -o "$OUT/p65_g0_probe_module.elf" "$OUT/p65_g0_probe.o"

"$CLI" verify "$OUT/p65_g0_probe_module.elf" \
    --target "$TARGET_ID" --targets-dir "$ROOT/targets"
cp "$OUT/p65_g0_probe_module.elf" "$FACE_DIR/p65_g0_probe.bin"

python3 - "$FACE_DIR" "$OUT/p65-g0-probe-resources.zip" <<'PY'
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo
import sys

face = Path(sys.argv[1])
output = Path(sys.argv[2])
names = ("main.lua", "p65_g0_probe.bin")
if any(not (face / name).is_file() for name in names):
    raise SystemExit("watchface resource root is incomplete")
output.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(output, "w") as bundle:
    for name in names:
        info = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        info.create_system = 3
        info.external_attr = 0o100644 << 16
        bundle.writestr(info, (face / name).read_bytes(),
                        compress_type=ZIP_DEFLATED, compresslevel=9)
print("flat watchface resources:", output)
print("files:", ", ".join(names))
PY

python3 - "$OUT/p65_g0_probe_module.elf" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = path.read_bytes()
print("probe sha256:", sha256(data).hexdigest())
print("probe bytes:", len(data))
PY
