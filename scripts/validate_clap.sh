#!/bin/zsh
# clap-validator run (non-blocking in v0.1: skips cleanly when unavailable).
# Usage: scripts/validate_clap.sh [builddir]
#
# Release assets are VERSIONED (clap-validator-<ver>-macos-universal.tar.gz, binary nested
# under binaries/), so the old "latest/download/clap-validator-macos.tar.gz" URL 404s.
# We resolve the current macOS asset via the GitHub API and fall back to a pinned
# known-good URL, then to a clean SKIP when offline/unavailable.
set -uo pipefail
BUILD_DIR="${1:-build/full}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$REPO/scripts/.cache"
CLAP="$(/usr/bin/find "$REPO/$BUILD_DIR" -name "FilterTableUS.clap" -maxdepth 6 -print -quit)"
if [ -z "$CLAP" ]; then
  echo "SKIP: no FilterTableUS.clap found under $BUILD_DIR (CLAP wrapper disabled?)"
  exit 0
fi

PINNED_URL="https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-macos-universal.tar.gz"

find_validator() {
  /usr/bin/find "$CACHE" -name "clap-validator" -type f -maxdepth 4 -print -quit 2>/dev/null
}

VALIDATOR="$(find_validator)"
if [ -z "$VALIDATOR" ]; then
  mkdir -p "$CACHE"
  echo "Resolving clap-validator macOS release asset..."
  URL="$(curl -fsSL --max-time 30 "https://api.github.com/repos/free-audio/clap-validator/releases/latest" 2>/dev/null \
        | sed -n 's/.*"browser_download_url": *"\([^"]*macos[^"]*\.tar\.gz\)".*/\1/p' | head -1)"
  [ -n "$URL" ] || URL="$PINNED_URL"
  echo "Downloading $URL"
  if curl -fsSL --max-time 120 --retry 2 -o "$CACHE/clap-validator.tar.gz" "$URL"; then
    (cd "$CACHE" && tar xzf clap-validator.tar.gz) || true
  fi
  VALIDATOR="$(find_validator)"
fi
[ -n "$VALIDATOR" ] && chmod +x "$VALIDATOR" 2>/dev/null

if [ -z "$VALIDATOR" ] || ! "$VALIDATOR" --version >/dev/null 2>&1; then
  echo "SKIP: clap-validator unavailable — CLAP validation NOT run (non-blocking)"
  exit 0
fi

echo "Running $("$VALIDATOR" --version) on $CLAP"
"$VALIDATOR" validate "$CLAP"
rc=$?
if [ $rc -eq 0 ]; then
  echo "PASS: clap-validator"
else
  echo "FAIL: clap-validator rc=$rc"
  echo "NOTE: known v0.1 issue — 'param-conversions' fails because the frozen parameter layout"
  echo "      (source/plugin/Parameters.cpp) has no explicit stringFromValue functions, so float"
  echo "      text round-trips at full precision are inconsistent (e.g. Env Sensitivity). The"
  echo "      Wave-3 integration agent owns that fix; run_all_checks treats this step as advisory"
  echo "      until then. This script's exit code stays honest."
fi
exit $rc
