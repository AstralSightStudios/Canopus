#!/bin/sh
# Device gate harness (CAN-REL-003). Runs one gate or the full G0-G13 sequence
# against a target device. The device steps are intentionally minimal: each
# gate is defined in tests/hardware/gates.md; the host-verifiable preconditions
# (build/package/verifier) must already have passed via scripts/ci.sh.
#
# Usage:
#   scripts/device-gates.sh <gate|all> <device-addr>
#
# On a unit, this is where the operator runs each gate and records the result
# JSON under tests/hardware/runs/.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GATE=$1
ADDR=${2:-}

# --- host preconditions that must hold before ANY device gate ---------
"$ROOT/scripts/ci.sh" >/dev/null 2>&1 || {
    echo "device gates require all host CI gates to pass first"
    exit 1
}

echo "device gate harness: running '$GATE'${ADDR:+ against $ADDR}"
echo "  see tests/hardware/gates.md for the exact device steps"
echo "  results must be archived per architecture §20.4 before the next gate"
echo "  (this script is the operator checklist; device execution is manual)"

case "$GATE" in
    all)
        for g in 0 1 2 3 4 5 6 7 8 9 10 11 12 13; do
            echo "  -> gate G$g pending (manual): see tests/hardware/gates.md"
        done
        ;;
    [0-9]|1[0-3])
        echo "  -> gate G$GATE pending (manual): see tests/hardware/gates.md"
        ;;
    *)
        echo "unknown gate '$GATE' (expected 0..13 or 'all')" >&2
        exit 2
        ;;
esac
