#!/usr/bin/env bash
# Called by mirage.ps1; arguments stay separate from shell source.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export PATH="$HOME/.cargo/bin:$PATH"
export CANOPUS_ROOT="$ROOT"
action=$1
shift
case "$action" in
  canopus)
    if [ ! -x target/debug/canopus ]; then cargo build -p canopus-cli; fi
    exec target/debug/canopus "$@"
    ;;
  supervisor)
    export CANOPUS_TARGET=${1:?Usage: mirage.ps1 supervisor TARGET_ID}
    shift
    for option in "$@"; do
      case "$option" in
        --headless) export CANOPUS_SUP_EXTRA_DEFINES="${CANOPUS_SUP_EXTRA_DEFINES:-} -DCANOPUS_SUP_HEADLESS=1" ;;
        --min-size) export CANOPUS_SUP_EXTRA_DEFINES="${CANOPUS_SUP_EXTRA_DEFINES:-} -Oz" ;;
        *) echo "Usage: mirage.ps1 supervisor TARGET_ID [--headless] [--min-size]" >&2; exit 2 ;;
      esac
    done
    exec sh scripts/build_canopus_supervisor.sh
    ;;
  *) echo "Unknown WSL action: $action" >&2; exit 2 ;;
esac
