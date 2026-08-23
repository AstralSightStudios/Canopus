#!/bin/sh
# Canopus CI gate script — runs every host gate in order (CAN-REL-002).
# CI (GitHub Actions) and local dev both use this; a failure stops the run.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

step() { echo; echo "==> $1"; }

step "1/6 root workspace tests (schemas, CLI, core, verifier, package, re)"
cargo test --workspace
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/fw-match/tests -p 'test_*.py'
# `cargo test` builds test harnesses under target/debug/deps, but the module
# verifier scripts invoke the standalone CLI at target/debug/canopus.
cargo build --package canopus-cli

step "2/6 Rust SDK workspace tests"
private_generated=$(mktemp)
for target in xiaomi-band-10-pro-3.101.036 xiaomi-band-10-pro-3.101.043; do
    case "$target" in
        xiaomi-band-10-pro-3.101.036) suffix=1036 ;;
        xiaomi-band-10-pro-3.101.043) suffix=1043 ;;
        *) echo "unknown generated target: $target" >&2; exit 1 ;;
    esac
    python3 tools/generate_target_private_symbols.py \
        --target-id "$target" \
        --symbols-dir "targets/$target/symbols" \
        --generated-rust "sdk/rust/canopus-target-generated/src/generated_${suffix}.rs" \
        --output "$private_generated"
    if ! cmp -s "$private_generated" "sdk/rust/canopus-target-private/src/generated_symbols_${suffix}.rs"; then
        echo "generated target-private metadata is stale for $target; rerun tools/generate_target_private_symbols.py" >&2
        rm -f "$private_generated"
        exit 1
    fi
done
rm -f "$private_generated"
(cd sdk/rust && cargo test --workspace)
for feature in \
    target-xiaomi-band-9-pro-3-1-175 \
    target-xiaomi-band-11-4-100-108 \
    target-xiaomi-band-9-3-1-32; do
    (cd sdk/rust && cargo check -p canopus-target-generated --no-default-features --features "$feature")
done
# Private firmware calls must never compile without one exact target backend.
if private_output=$(cd sdk/rust && cargo check -p canopus-target-private --no-default-features 2>&1); then
    echo "target-private unexpectedly compiled without a target feature" >&2
    exit 1
fi
printf '%s\n' "$private_output" | grep -q "requires exactly one target-" || {
    printf '%s\n' "$private_output" >&2
    echo "target-private failed for an unexpected reason" >&2
    exit 1
}

step "3/6 C host tests"
(cd tests/host && make clean >/dev/null && make >/dev/null && ./canopus_host_tests)

step "4/6 bare-metal portable runtime cross-thumb sanity"
(cd tests/host && make cross-thumb)

step "5/6 C module cross-build + verifier (hello)"
./modules/examples/hello/build.sh

step "6/6 Rust module cross-build + verifier (no-heap-counter)"
./sdk/rust/examples/no-heap-counter/build.sh

step "extra: watchface Lua tests (only if a Lua interpreter exists)"
if command -v lua >/dev/null 2>&1; then
    lua scripts/lua/watchface_smoke.lua
    lua scripts/lua/test_installer_firmware_selection.lua
else
    echo "  (no lua interpreter; skipping)"
fi

step "loader target profile gating"
loader_tmp=$(mktemp -d)
python3 scripts/generate_band9_loader_config.py \
    --profile targets/xiaomi-band-9-pro-3.1.175/loader/bootstrap.toml \
    --target-toml targets/xiaomi-band-9-pro-3.1.175/target.toml \
    --header "$loader_tmp/9175.h" --lua "$loader_tmp/9175.lua"
python3 scripts/generate_band9_loader_config.py \
    --profile targets/xiaomi-band-9-3.1.32/loader/bootstrap.toml \
    --target-toml targets/xiaomi-band-9-3.1.32/target.toml \
    --header "$loader_tmp/9132.h" --lua "$loader_tmp/9132.lua"
python3 - \
    targets/xiaomi-band-9-3.1.32/loader/bootstrap.toml \
    "$loader_tmp/pending.toml" <<'PY'
import pathlib, sys
source = pathlib.Path(sys.argv[1]).read_text()
pathlib.Path(sys.argv[2]).write_text(
    source.replace('status = "STATIC_RECOVERED"', 'status = "PENDING"', 1)
)
PY
if python3 scripts/generate_band9_loader_config.py \
    --profile "$loader_tmp/pending.toml" \
    --target-toml targets/xiaomi-band-9-3.1.32/target.toml \
    --header "$loader_tmp/pending.h"; then
    rm -rf "$loader_tmp"
    echo "PENDING loader profile unexpectedly emitted executable config" >&2
    exit 1
fi
rm -rf "$loader_tmp"


step "7/7 plan status table validation (CAN-P1-015)"
./scripts/check-plan-status.sh

echo
echo "all CI gates passed"
