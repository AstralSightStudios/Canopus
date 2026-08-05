#!/bin/sh
# Canopus CI gate script — runs every host gate in order (CAN-REL-002).
# CI (GitHub Actions) and local dev both use this; a failure stops the run.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

step() { echo; echo "==> $1"; }

step "1/6 root workspace tests (schemas, CLI, core, verifier, package, re)"
cargo test --workspace

step "2/6 Rust SDK workspace tests"
(cd sdk/rust && cargo test --workspace)

step "3/6 C host tests"
(cd tests/host && make clean >/dev/null && make >/dev/null && ./canopus_host_tests)

step "4/6 bare-metal portable runtime cross-thumb sanity"
(cd tests/host && make cross-thumb)

step "5/6 C module cross-build + verifier (hello)"
./modules/examples/hello/build.sh

step "6/6 Rust module cross-build + verifier (no-heap-counter)"
./sdk/rust/examples/no-heap-counter/build.sh

step "extra: watchface Lua smoke (only if a Lua interpreter exists)"
if command -v lua >/dev/null 2>&1; then
    lua scripts/lua/watchface_smoke.lua
else
    echo "  (no lua interpreter; skipping)"
fi

step "7/7 plan status table validation (CAN-P1-015)"
./scripts/check-plan-status.sh

echo
echo "all CI gates passed"
