#!/bin/zsh
# auval validation of the AU (aumf Ftbl FtUs). The component must be installed in
# ~/Library/Audio/Plug-Ins/Components (COPY_PLUGIN_AFTER_BUILD does this at build time).
set -uo pipefail
COMPONENT="$HOME/Library/Audio/Plug-Ins/Components/FilterTableUS.component"
if [ ! -d "$COMPONENT" ]; then
  echo "FAIL: $COMPONENT not installed (build with COPY_PLUGIN_AFTER_BUILD, or copy manually)"
  exit 1
fi
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1
auval -strict -v aumf Ftbl FtUs
rc=$?
if [ $rc -eq 0 ]; then echo "PASS: auval (aumf Ftbl FtUs)"; else echo "FAIL: auval rc=$rc"; fi
exit $rc
