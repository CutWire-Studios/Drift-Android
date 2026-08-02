#!/usr/bin/env bash
# Milestone-2 check: run the compositor on the device with no UI and pull back the frame.
#
#   scripts/selftest.sh <project.json> <media...>
#
# Pushes a plain project JSON as selftest.json plus its media into the app's external files dir —
# app-owned, no permission needed at any API level, and a real filesystem path avformat_open_input
# opens directly. main.cpp sees selftest.json, composites one frame, writes selftest.png and exits.
#
# Compare the result against the desktop for the same project and timestamp:
#   ./build/host/tools/renderframe project.json 0 expected.png
#   compare -metric RMSE expected.png selftest.png null:
set -euo pipefail

PKG="org.cutwire.drift"
REMOTE="/sdcard/Android/data/$PKG/files"

[ $# -ge 1 ] || { echo "usage: $0 <project.json> [media...]" >&2; exit 1; }
PROJECT="$1"; shift

adb shell mkdir -p "$REMOTE"
adb push "$PROJECT" "$REMOTE/selftest.json"
for f in "$@"; do adb push "$f" "$REMOTE/"; done

adb shell rm -f "$REMOTE/selftest.png"
adb logcat -c
adb shell am start -n "$PKG/org.qtproject.qt.android.bindings.QtActivity" >/dev/null

echo "==> waiting for selftest.png"
for _ in $(seq 1 60); do
    if adb shell "test -f $REMOTE/selftest.png" 2>/dev/null; then
        adb pull "$REMOTE/selftest.png" .
        echo "==> pulled selftest.png"
        break
    fi
    sleep 1
done

# GlRuntime qWarns with a "GlRuntime:" prefix on every failure path, and the shader messages carry
# the package id and pass index, so a broken effect names itself.
echo "==> log"
adb logcat -d Qt:V DEBUG:V AndroidRuntime:E '*:S' | tail -40
