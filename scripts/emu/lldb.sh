#!/bin/sh
# Attach lldb to a running Mirage device's gdb stub (no gdb is needed).
#   scripts/emu/lldb.sh <device-id-prefix> [module.elf <text-load-address>]
# Attaching stops the CPU; `continue` or `detach` resumes it.
set -eu
dev="$1"
sock=$(ls /var/folders/*/*/T/mirage-"$dev"*/gdb.sock 2>/dev/null | head -1)
[ -n "$sock" ] || { echo "no gdb.sock for device '$dev' (running? use the id prefix from 'miragectl list')" >&2; exit 1; }
if [ $# -ge 3 ]; then
    exec lldb -o "platform select remote-gdb-server" -o "process connect --plugin gdb-remote unix-connect://$sock" \
        -o "target modules add $2" -o "target modules load --file $2 .text $3"
fi
exec lldb -o "platform select remote-gdb-server" -o "process connect --plugin gdb-remote unix-connect://$sock"
