#!/bin/sh
# Run inside an MSYS2 UCRT64 shell with Mirage's documented build dependencies.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORKSPACE=$(cd "$ROOT/.." && pwd)
MIRAGE=${MIRAGE_ROOT:-$WORKSPACE/Mirage}
SRC=$MIRAGE/External/qemu-miwear
PATCH=$ROOT/scripts/emu/qemu-nor-shutdown.patch
DEST=$(realpath -m "$WORKSPACE/.tools/mirage/qemu-fixed")
# The upstream staging script removes DEST recursively; constrain that target.
if [ "$DEST" != "$WORKSPACE/.tools/mirage/qemu-fixed" ]; then
    echo "unexpected QEMU staging path: $DEST" >&2
    exit 1
fi
if git -c core.autocrlf=true -C "$SRC" apply --check "$PATCH" 2>/dev/null; then
    git -c core.autocrlf=true -C "$SRC" apply "$PATCH"
elif ! git -c core.autocrlf=true -C "$SRC" apply --reverse --check "$PATCH"; then
    echo "QEMU source does not match the shutdown fix" >&2
    exit 1
fi
sh "$MIRAGE/Scripts/build-qemu-windows.sh" "$DEST"
