#!/usr/bin/env bash
# Regenerate Android launcher mipmaps + splash logo from resources/Drift_icon.png.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/resources/Drift_icon.png"
RES="$ROOT/android/res"

if [[ ! -f "$SRC" ]]; then
  echo "missing $SRC" >&2
  exit 1
fi

magick "$SRC" -resize 256x256 "$ROOT/resources/drift.png"

declare -A SIZES=([mdpi]=48 [hdpi]=72 [xhdpi]=96 [xxhdpi]=144 [xxxhdpi]=192)
declare -A FG=([mdpi]=108 [hdpi]=162 [xhdpi]=216 [xxhdpi]=324 [xxxhdpi]=432)

for dens in mdpi hdpi xhdpi xxhdpi xxxhdpi; do
  mkdir -p "$RES/mipmap-${dens}" "$RES/drawable-${dens}"
  magick "$SRC" -resize "${SIZES[$dens]}x${SIZES[$dens]}" \
    "$RES/mipmap-${dens}/ic_launcher.png"
  cp "$RES/mipmap-${dens}/ic_launcher.png" "$RES/mipmap-${dens}/ic_launcher_round.png"
  canvas=${FG[$dens]}
  icon=$(( canvas * 80 / 100 ))
  magick "$SRC" -resize "${icon}x${icon}" \
    -gravity center -background none -extent "${canvas}x${canvas}" \
    "$RES/drawable-${dens}/ic_launcher_foreground.png"
done

mkdir -p "$RES/drawable"
magick "$SRC" -resize 512x512 "$RES/drawable/splash_logo.png"
echo "regenerated launcher icons and splash_logo from $SRC"
