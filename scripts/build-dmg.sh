#!/usr/bin/env bash
# Package PredictablePinyin.app into a distributable .dmg.
#
# The bundle is ad-hoc signed — same as the install script. End users install
# into their own ~/Library/Input Methods/, which does not require Developer
# ID signing or notarization. Recipients should mount the .dmg and run
# ./scripts/install-macos.sh (or drag the bundle into ~/Library/Input Methods/
# and invoke `--register-input-source` themselves).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

app_name="PredictablePinyin"
build_dir="${repo_root}/build"
src_app="${build_dir}/${app_name}.app"
dmg_out="${build_dir}/${app_name}.dmg"

if command -v brew >/dev/null 2>&1; then
  export PKG_CONFIG_PATH="$(brew --prefix librime)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
fi

"${repo_root}/scripts/build-icns.sh"

if [[ ! -d "${build_dir}" ]]; then
  cmake -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
fi
"${repo_root}/scripts/build.sh"

if [[ ! -d "${src_app}" ]]; then
  echo "Build did not produce ${src_app}" >&2
  exit 1
fi

codesign --force --deep --sign - "${src_app}"

rm -f "${dmg_out}"
hdiutil create \
  -volname "Predictable Pinyin" \
  -srcfolder "${src_app}" \
  -ov -format UDZO "${dmg_out}"

echo "Wrote ${dmg_out}"
