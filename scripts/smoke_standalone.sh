#!/bin/zsh
# Launch the Standalone app briefly and screenshot it. Usage: scripts/smoke_standalone.sh [builddir]
set -uo pipefail
BUILD_DIR="${1:-build/full}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
APP="$REPO/$BUILD_DIR/FilterTableUS_artefacts/Release/Standalone/FilterTableUS.app"
[ -d "$APP" ] || APP="$REPO/$BUILD_DIR/FilterTableUS_artefacts/Standalone/FilterTableUS.app"
[ -d "$APP" ] || { echo "FAIL: standalone app not found under $BUILD_DIR"; exit 1; }

mkdir -p "$REPO/build/artifacts"
open "$APP"
sleep 4
OUT="$REPO/build/artifacts/standalone-$(date +%H%M%S).png"
screencapture -x "$OUT" || true
osascript -e 'quit app "FilterTableUS"' 2>/dev/null || pkill -f "FilterTableUS.app" || true
if [ -f "$OUT" ]; then echo "PASS: screenshot at $OUT"; else echo "WARN: launched but no screenshot captured"; fi
