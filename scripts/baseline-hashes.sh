#!/bin/sh
# Prints SHA-256 baselines for refactor comparison (refactor-plan P0.6):
#   committed  - tracked generated artifacts
#   generator  - what the current generators emit now
#   elf        - Supervisor + example module ELFs per active target
# Usage: scripts/baseline-hashes.sh > docs/baseline/hashes.txt
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
sum() { shasum -a 256 "$@" | sed 's#  \./#  #'; }

echo "## committed"
git ls-files | grep -E 'generated/|generated_[0-9a-z]+\.rs|generated_symbols_' | LC_ALL=C sort | while read -r f; do sum "$f"; done

echo "## generator"
tmp=$(mktemp -d)
for t in $(ls targets | grep '^xiaomi-'); do
    cargo run -q -p canopus-cli -- target generate-veneer "$t" \
        --output "$tmp/$t.veneer.h" --config-output "$tmp/$t.config.h" >/dev/null
    (cd "$tmp" && sum "$t.veneer.h" "$t.config.h")
done
rm -rf "$tmp"

echo "## elf"
for t in xiaomi-band-10-pro-3.101.036 xiaomi-band-10-pro-3.101.043 \
         xiaomi-band-11-4.100.139 xiaomi-band-11-4.100.155 xiaomi-band-11-4.100.177; do
    if CANOPUS_TARGET=$t sh scripts/build_canopus_supervisor.sh >/dev/null 2>&1; then
        for f in watchfaces/canopus-installer/build/$t/*supervisor*.elf watchfaces/canopus-installer/build/$t/*supervisor*.bin; do
            [ -f "$f" ] && sum "$f"
        done
    else
        echo "SKIP  supervisor $t (build failed or needs firmware)"
    fi
done
for f in modules/examples/hello/build/hello_module.elf; do [ -f "$f" ] && sum "$f"; done
find sdk/rust/examples/no-heap-counter/build -name '*.elf' | LC_ALL=C sort | while read -r f; do sum "$f"; done
