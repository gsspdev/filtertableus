#!/bin/zsh
# pluginval validation of the VST3. Usage: scripts/validate_vst3.sh [builddir] [strictness]
set -uo pipefail
BUILD_DIR="${1:-build/full}"
STRICTNESS="${2:-8}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$REPO/scripts/.cache"
VST3="$REPO/$BUILD_DIR/FilterTableUS_artefacts/Release/VST3/FilterTableUS.vst3"
[ -d "$VST3" ] || VST3="$REPO/$BUILD_DIR/FilterTableUS_artefacts/VST3/FilterTableUS.vst3"
if [ ! -d "$VST3" ]; then
  echo "FAIL: VST3 not found under $BUILD_DIR (looked for FilterTableUS_artefacts/[Release/]VST3)"
  exit 1
fi

PLUGINVAL="$CACHE/pluginval.app/Contents/MacOS/pluginval"
if [ ! -x "$PLUGINVAL" ]; then
  mkdir -p "$CACHE"
  echo "Downloading pluginval..."
  curl -fsSL --max-time 120 --retry 2 -o "$CACHE/pluginval.zip" \
    "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip" || {
      echo "FAIL: could not download pluginval (offline?) — required validation NOT run"; exit 2; }
  (cd "$CACHE" && unzip -oq pluginval.zip)
fi
[ -x "$PLUGINVAL" ] || { echo "FAIL: pluginval binary missing after download"; exit 1; }

echo "Running pluginval --strictness-level $STRICTNESS on $VST3"
"$PLUGINVAL" --strictness-level "$STRICTNESS" --validate "$VST3" --timeout-ms 300000
rc=$?
if [ $rc -eq 0 ]; then echo "PASS: pluginval strictness $STRICTNESS"; else echo "FAIL: pluginval rc=$rc"; fi
exit $rc
