# CutWire Drift (Android)

Mobile video editor built with Qt 6 Quick. This tree targets Android
(`arm64-v8a` / `armeabi-v7a` / `x86_64`).

## Requirements

- Linux host
- [Qt 6.11.1](https://www.qt.io/download) with the matching Android kit and host tools  
  Defaults used by the scripts:
  - `~/Qt/6.11.1/android_arm64_v8a` (or `android_x86_64`)
  - `~/Qt/6.11.1/gcc_64`
- Android SDK (`~/Android/Sdk`)
- NDK **27.2.12479018** (the version the Qt kit expects — do not mix NDK majors)
- CMake, Ninja, a C++ toolchain for the host
- `adb` / `apksigner` for install (SDK platform-tools + build-tools)

## First-time native dependencies

FFmpeg, x264, zstd, OpenSSL, and SoundTouch are built for Android once and cached
under `third_party/prebuilt/`:

```bash
export ANDROID_NDK_ROOT=~/Android/Sdk/ndk/27.2.12479018
./third_party/build-android.sh arm64-v8a
```

`scripts/build.sh` runs this automatically when the prebuilts are missing.

## Build

```bash
# Optional overrides:
#   QT_ANDROID_ROOT  QT_HOST_PATH  ANDROID_SDK_ROOT  ANDROID_NDK_ROOT

./scripts/build.sh              # arm64-v8a, RelWithDebInfo
./scripts/build.sh arm64-v8a Release
./scripts/build.sh armeabi-v7a
./scripts/build.sh x86_64
```

The unsigned APK lands under `build/android-<abi>/android-build/`.

## Install on a device

```bash
# Signs with the Android debug keystore and adb install -r
./scripts/deploy.sh arm64-v8a
```

If packaging left a stale `android-build/drift.apk`, copy the fresh unsigned
release APK there first:

```bash
cp build/android-arm64-v8a/android-build/build/outputs/apk/release/android-build-release-unsigned.apk \
   build/android-arm64-v8a/android-build/drift.apk
./scripts/deploy.sh arm64-v8a
```

## Layout

| Path | Purpose |
|------|---------|
| `src/` | C++ engine, models, QML UI |
| `android/` | Android package overlay (manifest, splash, icons) |
| `scripts/` | Build / deploy helpers |
| `third_party/` | Dependency build script (sources & prebuilts are gitignored) |
| `effects/`, `transitions/`, … | Bundled effect packs |

## License

See [LICENSE](LICENSE) (GPL-3.0).
