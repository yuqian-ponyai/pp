#!/usr/bin/env bash
# Generate data/macos/PredictablePinyin.icns from data/icons/predictable-pinyin.png.
# No-op if the output is newer than the input.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_png="${repo_root}/data/icons/predictable-pinyin.png"
out_icns="${repo_root}/data/macos/PredictablePinyin.icns"

if [[ -f "$out_icns" && "$out_icns" -nt "$src_png" ]]; then
  exit 0
fi

if ! command -v iconutil >/dev/null 2>&1; then
  echo "iconutil not found (macOS-only)" >&2
  exit 1
fi
if ! command -v sips >/dev/null 2>&1; then
  echo "sips not found (macOS-only)" >&2
  exit 1
fi

iconset="$(mktemp -d)/PredictablePinyin.iconset"
mkdir -p "$iconset"
trap 'rm -rf "$(dirname "$iconset")"' EXIT

for spec in \
  "16 icon_16x16.png"          "32 icon_16x16@2x.png" \
  "32 icon_32x32.png"          "64 icon_32x32@2x.png" \
  "128 icon_128x128.png"       "256 icon_128x128@2x.png" \
  "256 icon_256x256.png"       "512 icon_256x256@2x.png" \
  "512 icon_512x512.png"       "1024 icon_512x512@2x.png"; do
  size="${spec%% *}"; name="${spec##* }"
  sips -z "$size" "$size" "$src_png" --out "$iconset/$name" >/dev/null
done

mkdir -p "$(dirname "$out_icns")"
iconutil -c icns "$iconset" -o "$out_icns"
echo "Wrote $out_icns"
