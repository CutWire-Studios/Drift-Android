#!/usr/bin/env bash
# Cross-builds the native dependencies Drift links that have no Android package: FFmpeg, x264,
# zstd, OpenSSL (libcrypto) and SoundTouch. Output goes to
# third_party/prebuilt/android/<abi>/{include,lib}, which is where the root CMakeLists looks.
#
# JUCE and the ONNX Runtime headers are NOT here: JUCE is a FetchContent source tree CMake builds
# in-tree, and the app never links ONNX Runtime at all — it dlopens one at startup, so only the
# headers matter and cmake/FetchOnnxRuntime.cmake downloads those.
#
# Most deps are static and PIC (the app is one .so QtLoader dlopens; a non-PIC archive linked
# into a .so fails with R_AARCH64_ADR_PREL_PG_HI21). OpenSSL is the exception: static libcrypto.a
# for signing, plus libcrypto_3.so/libssl_3.so listed in QT_ANDROID_EXTRA_LIBS for Qt Network TLS.
#
# Usage: third_party/build-android.sh [abi] [api]
#   abi  arm64-v8a (default) or x86_64
#   api  minimum API level, default 28 — must match minSdk in the root CMakeLists
set -euo pipefail

ABI="${1:-arm64-v8a}"
API="${2:-28}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/src"
OUT="$HERE/prebuilt/android/$ABI"

# Pinned to the version the desktop build is verified against (system ffmpeg is n8.1.2).
# Note "n8.0" is not a tag — FFmpeg tags are n8.0.3, n8.1, n8.1.2, ...
FFMPEG_TAG="n8.1.2"
X264_TAG="stable"
ZSTD_TAG="v1.5.7"
OPENSSL_TAG="openssl-3.6.3"
# Upstream tags this release "2.4.1"; there is no plain "2.4.0".
SOUNDTOUCH_TAG="2.4.1"

: "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT to the NDK version Qt for Android was built against}"

case "$ABI" in
  arm64-v8a) FF_ARCH=aarch64; TRIPLE=aarch64-linux-android;  OSSL_TARGET=android-arm64 ;;
  x86_64)    FF_ARCH=x86_64;  TRIPLE=x86_64-linux-android;   OSSL_TARGET=android-x86_64 ;;
  *) echo "unsupported ABI: $ABI (expected arm64-v8a or x86_64)" >&2; exit 1 ;;
esac

TC="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64"
export CC="$TC/bin/${TRIPLE}${API}-clang"
export CXX="$TC/bin/${TRIPLE}${API}-clang++"
export AR="$TC/bin/llvm-ar"
export RANLIB="$TC/bin/llvm-ranlib"
export STRIP="$TC/bin/llvm-strip"
export NM="$TC/bin/llvm-nm"
export PATH="$TC/bin:$PATH"

[ -x "$CC" ] || { echo "no compiler at $CC — is ANDROID_NDK_ROOT right?" >&2; exit 1; }

mkdir -p "$SRC" "$OUT"
JOBS="$(nproc)"

clone() { # clone <url> <tag> <dir>
  [ -d "$SRC/$3" ] || git clone --depth 1 --branch "$2" "$1" "$SRC/$3"
}

echo "==> building for $ABI (API $API) into $OUT"

# --- x264 --------------------------------------------------------------------
# Exporter's default H.264 preset. It probes avcodec_find_encoder_by_name at runtime, so this is
# not strictly required — without it the codec list simply comes up short.
clone https://code.videolan.org/videolan/x264.git "$X264_TAG" x264
( cd "$SRC/x264" && make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$OUT" --host="$TRIPLE" --enable-static --enable-pic \
      --disable-cli --disable-opencl --sysroot="$TC/sysroot"
  make -j"$JOBS" && make install )

# --- FFmpeg ------------------------------------------------------------------
# No --disable-postproc: libpostproc was removed in FFmpeg 8.0 and configure hard-errors on the
# option. avformat and avcodec are always built, so they have no --enable- switch either.
#
# -fvisibility=hidden is load-bearing, not tidiness. FFmpeg's aarch64 assembly reaches its constant
# tables with adrp/add (e.g. tx_float_neon.S -> ff_tx_tab_32_float). That addressing is only legal
# against a symbol the linker can resolve statically, and a *global* symbol in a shared object is
# preemptible, so linking these archives into libdrift.so fails with
#   relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol ...; recompile with -fPIC
# despite --enable-pic being set (the asm is already PIC — the symbols are the problem). Hidden
# visibility makes them non-preemptible and the relocation valid. Safe because FFmpeg is linked
# *into* the app's .so and its API is never re-exported.
# Decoders and encoders both: Exporter is compiled in, so the muxers and encoders have to exist for
# it to have anything to offer. MediaCodec is deliberately absent — see the plan; software decode
# is the milestone-1 path and the hardware one needs its own measurement pass.
clone https://git.ffmpeg.org/ffmpeg.git "$FFMPEG_TAG" ffmpeg
( cd "$SRC/ffmpeg" && make distclean >/dev/null 2>&1 || true
  PKG_CONFIG_LIBDIR="$OUT/lib/pkgconfig" ./configure \
    --prefix="$OUT" \
    --target-os=android --arch="$FF_ARCH" --enable-cross-compile \
    --sysroot="$TC/sysroot" \
    --cc="$CC" --cxx="$CXX" --ar="$AR" --nm="$NM" --ranlib="$RANLIB" --strip="$STRIP" \
    --enable-static --disable-shared --enable-pic \
    --enable-gpl --enable-version3 --enable-libx264 \
    --extra-cflags="-I$OUT/include -fvisibility=hidden" --extra-ldflags="-L$OUT/lib" \
    --disable-programs --disable-doc --disable-avdevice \
    --disable-vaapi --disable-vdpau --disable-v4l2-m2m --disable-cuda-llvm \
    --disable-vulkan --disable-libdrm --disable-xlib \
    --enable-avfilter --enable-swscale --enable-swresample
  make -j"$JOBS" && make install )

# --- zstd --------------------------------------------------------------------
# .driftpkg addons and .drift project bundles.
clone https://github.com/facebook/zstd.git "$ZSTD_TAG" zstd
cmake -S "$SRC/zstd/build/cmake" -B "$SRC/zstd/build-$ABI" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
  -DCMAKE_INSTALL_PREFIX="$OUT" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_PROGRAMS=OFF \
  -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_LEGACY_SUPPORT=OFF
cmake --build "$SRC/zstd/build-$ABI" --target install

# --- OpenSSL -----------------------------------------------------------------
# Static libcrypto.a: Ed25519 verification of .driftpkg signatures (AddonPackage.cpp).
# Shared libcrypto_3.so / libssl_3.so: Qt Network's qopensslbackend dlopens these at runtime.
# Android rejects versioned sonames (libssl.so.3), so we ship the unversioned _3 names Qt expects
# (same layout as KDAB/android_openssl). Requires patchelf to rewrite SONAME after the build.
clone https://github.com/openssl/openssl.git "$OPENSSL_TAG" openssl
( cd "$SRC/openssl" && make clean >/dev/null 2>&1 || true
  ANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" ./Configure "$OSSL_TARGET" \
      -D__ANDROID_API__="$API" --prefix="$OUT" --openssldir="$OUT/ssl" \
      shared no-tests no-apps no-engine
  make -j"$JOBS" SHLIB_VERSION_NUMBER= build_libs
  cp libcrypto.a "$OUT/lib/"
  cp -r include/openssl "$OUT/include/"
  if command -v patchelf >/dev/null 2>&1; then
    cp libcrypto.so "$OUT/lib/libcrypto_3.so"
    cp libssl.so "$OUT/lib/libssl_3.so"
    patchelf --set-soname libcrypto_3.so "$OUT/lib/libcrypto_3.so"
    patchelf --set-soname libssl_3.so "$OUT/lib/libssl_3.so"
    patchelf --replace-needed libcrypto.so libcrypto_3.so "$OUT/lib/libssl_3.so"
  else
    echo "warning: patchelf not found — leaving existing libcrypto_3.so/libssl_3.so in place" >&2
    echo "         (install patchelf, or copy ssl_3/$ABI/*.so from KDAB/android_openssl)" >&2
  fi )

# --- SoundTouch --------------------------------------------------------------
# Pitch shifting behind the voice effects. Sources include <soundtouch/SoundTouch.h>, so the
# headers must land in include/soundtouch — which is where its own install puts them.
clone https://codeberg.org/soundtouch/soundtouch.git "$SOUNDTOUCH_TAG" soundtouch
cmake -S "$SRC/soundtouch" -B "$SRC/soundtouch/build-$ABI" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
  -DCMAKE_INSTALL_PREFIX="$OUT" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF -DSOUNDSTRETCH=OFF -DSOUNDTOUCH_DLL=OFF
cmake --build "$SRC/soundtouch/build-$ABI" --target install

echo
echo "==> done. $OUT/lib:"
ls -1 "$OUT/lib"/*.a "$OUT/lib"/libcrypto_3.so "$OUT/lib"/libssl_3.so 2>/dev/null || ls -1 "$OUT/lib"
