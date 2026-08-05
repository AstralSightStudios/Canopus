#!/bin/sh
# CAN-P1-015: validates the P0/P1/P2 status table in
# docs/native-manager-ui-plan.md (§15). Every status must be one of the
# allowed values so the table never drifts into invented states, and a row
# whose status is still OPEN can never be read as production-ready.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TABLE="$ROOT/docs/native-manager-ui-plan.md"
ALLOWED="OPEN|BLOCKED-EVIDENCE|CLOSED-GATED|HOST-FIXED/DEVICE-PENDING|CLOSED"

grep -E '^\| CAN-P[0-9]+-[0-9]+ \| P[0-2] \|' "$TABLE" | while IFS= read -r row; do
    status=$(printf '%s\n' "$row" | awk -F'|' '{gsub(/^ +| +$/, "", $4); print $4}')
    if ! printf '%s\n' "$status" | grep -qE "^($ALLOWED)$"; then
        echo "bad status '$status' in: $row"
        exit 1
    fi
done
echo "plan status table OK"
