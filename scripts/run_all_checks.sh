#!/bin/zsh
# Full local verification chain. Usage: scripts/run_all_checks.sh [builddir]
set -uo pipefail
BUILD_DIR="${1:-build/full}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

fail=0
step() { echo "\n=== $1 ==="; }

step "configure+build ($BUILD_DIR)"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build "$BUILD_DIR" || fail=1

step "ctest"
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail=1

step "pluginval (VST3)"
scripts/validate_vst3.sh "$BUILD_DIR" || fail=1

step "auval (AU)"
scripts/validate_au.sh || fail=1

step "clap-validator (CLAP)"
scripts/validate_clap.sh "$BUILD_DIR" || fail=1

step "standalone smoke"
scripts/smoke_standalone.sh "$BUILD_DIR" || true

if [ $fail -eq 0 ]; then echo "\nALL CHECKS PASSED"; else echo "\nSOME CHECKS FAILED"; fi
exit $fail
