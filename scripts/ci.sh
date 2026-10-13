#!/bin/sh
# Canopus CI gate script — runs every host gate in order (CAN-REL-002).
# CI (GitHub Actions) and local dev both use this; a failure stops the run.
# CANOPUS_CI_KEEP_GOING=1 runs every step and prints a green/RED table instead.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

step() { echo; echo "==> $1"; }

gate_0() {
    step "1/6 root workspace tests (schemas, CLI, core, verifier, package, re)"
    cargo test --workspace
    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/fw-match/tests -p 'test_*.py'
    # `cargo test` builds test harnesses under target/debug/deps, but the module
    # verifier scripts invoke the standalone CLI at target/debug/canopus.
    cargo build --package canopus-cli
}

gate_1() {
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
        target-xiaomi-band-11-4-100-139 \
        target-xiaomi-band-11-4-100-155 \
        target-xiaomi-band-11-4-100-177; do
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
}

gate_2() {
    step "3/6 C host tests"
    (cd tests/host && make clean >/dev/null && make >/dev/null && ./canopus_host_tests)
}

gate_3() {
    step "4/6 bare-metal portable runtime cross-thumb sanity"
    (cd tests/host && make cross-thumb)
}

gate_4() {
    step "5/6 C module cross-build + verifier (hello)"
    ./modules/examples/hello/build.sh
    # Load that module through the portable ELF loader with distinct code and data
    # bases, so a regression in the per-region base arithmetic shows up as a failed
    # constructor-pointer check rather than only on a device.
    (cd tests/host && make real-elf-loader MODULE_ELF=../../modules/examples/hello/build/hello_module.elf)
}

gate_5() {
    step "6/6 Rust module cross-build + verifier (no-heap-counter)"
    ./sdk/rust/examples/no-heap-counter/build.sh
}

gate_6() {
    step "extra: watchface Lua tests (only if a Lua interpreter exists)"
    if command -v lua >/dev/null 2>&1; then
        lua scripts/lua/watchface_smoke.lua
        lua scripts/lua/test_installer_firmware_selection.lua
    else
        echo "  (no lua interpreter; skipping)"
    fi
}

gate_7() {
    step "loader target profile gating"
    PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p 'test_*.py'
}

gate_8() {
    step "7/7 plan status table validation (CAN-P1-015)"
    ./scripts/check-plan-status.sh
}

if [ -n "${CANOPUS_CI_ONLY:-}" ]; then
    "$CANOPUS_CI_ONLY"
    exit
fi

if [ "${CANOPUS_CI_KEEP_GOING:-0}" = 1 ]; then
    rc=0
    summary=""
    for g in gate_0 gate_1 gate_2 gate_3 gate_4 gate_5 gate_6 gate_7 gate_8; do
        if CANOPUS_CI_ONLY=$g sh "$0"; then r=green; else r=RED; rc=1; fi
        summary="$summary
$r  $g"
    done
    echo; echo "==== CI summary ===="
    echo "$summary" | sed '/^$/d'
    exit $rc
fi

gate_0
gate_1
gate_2
gate_3
gate_4
gate_5
gate_6
gate_7
gate_8
echo
echo "all CI gates passed"
