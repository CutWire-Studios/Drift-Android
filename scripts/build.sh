#!/usr/bin/env bash
# Configures and builds the APK. Bootstraps the android/ package overlay from the Qt kit's template
# on first run, and the native prebuilt dependencies if they are not there yet.
#
#   scripts/build.sh [abi] [build-type]
#     abi         arm64-v8a (default), armeabi-v7a or x86_64
#     build-type  RelWithDebInfo (default), Debug, Release
#
# Environment:
#   QT_ANDROID_ROOT  Qt for Android kit, e.g. ~/Qt/6.11.1/android_arm64_v8a
#   QT_HOST_PATH     Host Qt of the SAME version, for moc/rcc/androiddeployqt. Defaults to
#                    /usr/lib/qt6 — override if the distro layout trips Qt6HostInfo.
#   ANDROID_SDK_ROOT ~/Android/Sdk
#   ANDROID_NDK_ROOT the NDK version the Qt kit was built against, NOT simply the newest installed.
#                    Mixing NDK majors between Qt, FFmpeg and the app produces libc++ symbol
#                    errors and dlopen failures that get misattributed to something else.
set -euo pipefail

ABI="${1:-arm64-v8a}"
BUILD_TYPE="${2:-RelWithDebInfo}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/android-$ABI"

QT_VERSION="6.11.1"
case "$ABI" in
  arm64-v8a)   QT_ABI_DIR="android_arm64_v8a" ;;
  # Qt names the 32-bit ARM kit after the architecture, not after the Android ABI.
  armeabi-v7a) QT_ABI_DIR="android_armv7" ;;
  x86_64)      QT_ABI_DIR="android_x86_64" ;;
  *) echo "unsupported ABI: $ABI (expected arm64-v8a, armeabi-v7a or x86_64)" >&2; exit 1 ;;
esac

: "${QT_ANDROID_ROOT:=$HOME/Qt/$QT_VERSION/$QT_ABI_DIR}"
: "${ANDROID_SDK_ROOT:=$HOME/Android/Sdk}"
# The version every module manifest in the Qt kit names. Not the newest installed: mixing NDK
# majors between Qt, FFmpeg and the app gives libc++ symbol errors and dlopen failures that look
# like something else entirely. Re-derive after a Qt upgrade with:
#   grep -rhoE '"[0-9]+\.[0-9]+\.[0-9]{7,}"' $QT_ANDROID_ROOT/modules/*.json | sort -u
: "${ANDROID_NDK_ROOT:=$ANDROID_SDK_ROOT/ndk/27.2.12479018}"
# The kit ships its own matching host Qt, which beats guessing at a distro layout.
: "${QT_HOST_PATH:=$HOME/Qt/$QT_VERSION/gcc_64}"

# Package identity. The defaults are the release app; CI overrides them so its APKs install
# alongside a release build rather than being refused for a signature mismatch. See the
# DRIFT_ANDROID_* block in the root CMakeLists.
: "${DRIFT_ANDROID_PACKAGE_NAME:=org.cutwire.drift}"
: "${DRIFT_ANDROID_APP_NAME:=Drift}"
: "${DRIFT_ANDROID_VERSION_CODE:=1}"

[ -d "$ANDROID_NDK_ROOT" ] || { echo "no NDK at $ANDROID_NDK_ROOT" >&2; exit 1; }
export ANDROID_NDK_ROOT ANDROID_SDK_ROOT

QT_CMAKE="$QT_ANDROID_ROOT/bin/qt-cmake"
[ -x "$QT_CMAKE" ] || { echo "no qt-cmake at $QT_CMAKE" >&2; exit 1; }

# --- android/ overlay --------------------------------------------------------
# Must come from the kit's own template: it declares QtActivity and the android.app.lib_name
# meta-data that tells QtLoader which .so to dlopen. A manifest naming a plain Activity installs
# fine and then dies with ClassNotFoundException.
if [ ! -f "$ROOT/android/AndroidManifest.xml" ]; then
    TEMPLATE="$QT_ANDROID_ROOT/src/android/templates/AndroidManifest.xml"
    [ -f "$TEMPLATE" ] || { echo "no manifest template at $TEMPLATE" >&2; exit 1; }
    mkdir -p "$ROOT/android"
    cp "$TEMPLATE" "$ROOT/android/AndroidManifest.xml"
    echo "bootstrapped android/AndroidManifest.xml from the Qt template."
    echo "Edit it before shipping: label, icon, and"
    echo "  <uses-feature android:glEsVersion=\"0x00030000\" android:required=\"true\"/>"
    echo "Declare no storage permissions — the SAF picker grants per-URI access without them."
fi

# --- native dependencies -----------------------------------------------------
if [ ! -f "$ROOT/third_party/prebuilt/android/$ABI/lib/libavcodec.a" ]; then
    echo "==> no prebuilt dependencies for $ABI; building them first"
    "$ROOT/third_party/build-android.sh" "$ABI"
fi

# --- configure and build -----------------------------------------------------
"$QT_CMAKE" -G Ninja -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DQT_HOST_PATH="$QT_HOST_PATH" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DQT_ANDROID_ABIS="$ABI" \
    -DDRIFT_BUNDLE_ONNXRUNTIME=OFF \
    -DDRIFT_ANDROID_PACKAGE_NAME="$DRIFT_ANDROID_PACKAGE_NAME" \
    -DDRIFT_ANDROID_APP_NAME="$DRIFT_ANDROID_APP_NAME" \
    -DDRIFT_ANDROID_VERSION_CODE="$DRIFT_ANDROID_VERSION_CODE"

cmake --build "$BUILD" --parallel
cmake --build "$BUILD" --target apk

echo
echo "==> APK:"
find "$BUILD/android-build" -name '*.apk' -newer "$BUILD/CMakeCache.txt" 2>/dev/null || true
