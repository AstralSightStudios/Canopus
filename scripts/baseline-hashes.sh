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
git ls-files | grep -E '^targets/[^/]+/generated/|/generated_xiaomi_[^/]+\.rs$|/generated_symbols_xiaomi_[^/]+\.rs$' | LC_ALL=C sort | while read -r f; do sum "$f"; done

echo "## generator"
tmp=$(mktemp -d)
cp -R targets "$tmp/targets"
mkdir -p "$tmp/sdk"
cp -R sdk/rust "$tmp/sdk/rust"
cargo run -q -p canopus-cli -- target generate --all --targets-dir "$tmp/targets" >/dev/null
(cd "$tmp" && git -C "$ROOT" ls-files | grep -E '^targets/[^/]+/generated/|/generated_xiaomi_[^/]+\.rs$|/generated_symbols_xiaomi_[^/]+\.rs$' | LC_ALL=C sort | while read -r f; do [ -f "$f" ] && sum "$f"; done)
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
for f in modules/examples/hello/build/xiaomi-band-10-pro-3.101.036/hello.elf modules/examples/hello-portable/build/hello_portable.elf; do [ -f "$f" ] && sum "$f"; done
find sdk/rust/examples/no-heap-counter/build -name '*.elf' | LC_ALL=C sort | while read -r f; do sum "$f"; done
