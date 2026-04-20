#!/usr/bin/env bash
# Downloads external data files into data/raw/ if they are missing or stale.
# Safe to re-run: skips files whose SHA256 already matches.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$repo_root/data/raw"
mkdir -p "$dest"

# Each entry: local_name  sha256  url
FILES=(
  "hanzi_db.csv"
  "fde27fd1cdc78de4e924f68e1c2750c8a30964d25e5cc75478bb01631b7169cf"
  "https://raw.githubusercontent.com/ruddfawcett/hanziDB.csv/master/hanzi_db.csv"

  "pinyin_simp.dict.yaml"
  "e341598343a0f0f2035bb1aafc34a7f3bb7887deeecb3f60796262aaa2983e6b"
  "https://raw.githubusercontent.com/rime/rime-pinyin-simp/master/pinyin_simp.dict.yaml"
)

ok=0
total=$(( ${#FILES[@]} / 3 ))

for (( i=0; i<${#FILES[@]}; i+=3 )); do
  name="${FILES[i]}"
  expected="${FILES[i+1]}"
  url="${FILES[i+2]}"
  path="$dest/$name"

  if [[ -f "$path" ]]; then
    actual="$(sha256sum "$path" | awk '{print $1}')"
    if [[ "$actual" == "$expected" ]]; then
      (( ok++ )) || true
      continue
    fi
    echo "$name: checksum mismatch, re-downloading ..."
  fi

  echo "Downloading $name ..."
  curl -fSL --retry 3 -o "$path" "$url"

  actual="$(sha256sum "$path" | awk '{print $1}')"
  if [[ "$actual" != "$expected" ]]; then
    echo "ERROR: $name checksum mismatch after download" >&2
    echo "  expected: $expected" >&2
    echo "  got:      $actual" >&2
    exit 1
  fi
  (( ok++ )) || true
done

echo "data/raw: $ok/$total files ready."

# stroke.dict.yaml is transformed from cnchar (MIT) by a Dart generator rather
# than downloaded as-is. The generator pins cnchar to a specific commit, verifies
# SHA256 of the input, and no-ops if the output already exists.
# See scripts/bin/build_stroke_map.dart and doc/stroke-data.md.
(
  cd "$repo_root/scripts"
  fvm dart pub get >/dev/null
  fvm dart run bin/build_stroke_map.dart
)
