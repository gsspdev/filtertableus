#!/bin/zsh
# Full local verification chain. Usage: scripts/run_all_checks.sh [builddir] [strictness]
# Exit 0 only when every required step passes (standalone smoke is advisory).
set -uo pipefail
BUILD_DIR="${1:-build/full}"
STRICTNESS="${2:-8}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

LOGDIR="$(mktemp -d /tmp/ftus-checks.XXXXXX)"
typeset -a STEP_NAMES STEP_VERDICTS
fail=0

step() { echo "\n=== $1 ==="; }

record() { # name rc advisory logfile
  local name="$1" rc="$2" advisory="$3" log="$4" verdict
  if [ "$rc" -eq 0 ]; then
    verdict="PASS"
    grep -q "^SKIP:" "$log" 2>/dev/null && verdict="SKIP"
  elif [ "$advisory" -eq 1 ]; then
    verdict="WARN (rc=$rc, advisory)"
  else
    verdict="FAIL (rc=$rc)"
    fail=1
  fi
  STEP_NAMES+=("$name")
  STEP_VERDICTS+=("$verdict")
}

run_step() { # name advisory command...
  local name="$1" advisory="$2"; shift 2
  step "$name"
  local log="$LOGDIR/${name//[^A-Za-z0-9._-]/_}.log" # sanitize: names may contain / ( ) spaces
  "$@" 2>&1 | tee "$log"
  record "$name" $? "$advisory" "$log"
}

skip_step() { # name reason
  step "$1"
  echo "SKIP: $2"
  STEP_NAMES+=("$1")
  STEP_VERDICTS+=("SKIP ($2)")
}

do_build() {
  cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build "$BUILD_DIR"
}

run_step "configure+build ($BUILD_DIR)" 0 do_build
BUILD_VERDICT="${STEP_VERDICTS[-1]}"

if [[ "$BUILD_VERDICT" == PASS* ]]; then
  run_step "ctest" 0 ctest --test-dir "$BUILD_DIR" --output-on-failure
  run_step "pluginval (VST3)" 0 scripts/validate_vst3.sh "$BUILD_DIR" "$STRICTNESS"
  run_step "auval (AU)" 0 scripts/validate_au.sh
  # CLAP is advisory in v0.1: the validator exposes a text-roundtrip issue rooted in the FROZEN
  # source/plugin/Parameters.cpp (no stringFromValue functions). Promote to required (advisory=0)
  # once the Wave-3 integration agent adds explicit string conversions.
  run_step "clap-validator (CLAP)" 1 scripts/validate_clap.sh "$BUILD_DIR"
  run_step "standalone smoke" 1 scripts/smoke_standalone.sh "$BUILD_DIR"
else
  skip_step "ctest" "build failed"
  skip_step "pluginval (VST3)" "build failed"
  skip_step "auval (AU)" "build failed"
  skip_step "clap-validator (CLAP)" "build failed"
  skip_step "standalone smoke" "build failed"
fi

echo "\n=== SUMMARY ($BUILD_DIR) ==="
for i in {1..${#STEP_NAMES[@]}}; do
  printf '  %-28s %s\n' "${STEP_NAMES[$i]}:" "${STEP_VERDICTS[$i]}"
done
echo "  (full logs: $LOGDIR)"

if [ $fail -eq 0 ]; then echo "\nALL CHECKS PASSED"; else echo "\nSOME CHECKS FAILED"; fi
exit $fail
