#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cmake -S "$root" -B "$work/build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$work/build" --parallel 4
ctest --test-dir "$work/build" --output-on-failure
