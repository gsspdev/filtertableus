#!/bin/zsh
# clap-validator run (non-blocking in v0.1: skips cleanly when unavailable).
# Usage: scripts/validate_clap.sh [builddir]
set -uo pipefail
BUILD_DIR="${1:-build/full}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$REPO/scripts/.cache"
CLAP="$(/usr/bin/find "$REPO/$BUILD_DIR" -name "FilterTableUS.clap" -maxdepth 6 -print -quit)"
if [ -z "$CLAP" ]; then
  echo "SKIP: no FilterTableUS.clap found under $BUILD_DIR (CLAP wrapper disabled?)"
  exit 0
fi

VALIDATOR="$CACHE/clap-validator"
if [ ! -x "$VALIDATOR" ]; then
  mkdir -p "$CACHE"
  echo "Attempting clap-validator download..."
  curl -fsSL --max-time 120 --retry 2 -o "$CACHE/clap-validator.tar.gz" \
    "https://github.com/free-audio/clap-validator/releases/latest/download/clap-validator-macos.tar.gz" \
    && (cd "$CACHE" && tar xzf clap-validator.tar.gz && chmod +x clap-validator 2>/dev/null) || true
fi
if [ ! -x "$VALIDATOR" ]; then
  # some releases nest a binary in a subdir
  FOUND="$(/usr/bin/find "$CACHE" -name clap-validator -type f -maxdepth 3 -print -quit 2>/dev/null)"
  [ -n "$FOUND" ] && chmod +x "$FOUND" && VALIDATOR="$FOUND"
fi
if [ ! -x "$VALIDATOR" ]; then
  echo "SKIP: clap-validator unavailable — CLAP validation NOT run (non-blocking)"
  exit 0
fi

"$VALIDATOR" validate "$CLAP"
rc=$?
if [ $rc -eq 0 ]; then echo "PASS: clap-validator"; else echo "FAIL: clap-validator rc=$rc"; fi
exit $rc
