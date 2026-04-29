#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if [[ -f "${repo_root}/env.sh" ]]; then
  # shellcheck disable=SC1091
  source "${repo_root}/env.sh"
fi

cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_INSTALL_PREFIX=/usr

"${repo_root}/scripts/build.sh"

rm -f predictable-pinyin-*.deb *.base64.sh

common_deps="librime1, librime-data-luna-pinyin, librime-data-pinyin-simp, librime-data-stroke"

# --- IBus .deb ---
cpack --config build/CPackConfig.cmake -G DEB \
  -D CPACK_COMPONENTS_ALL="common;ibus" \
  -D CPACK_COMPONENTS_GROUPING=ALL_COMPONENTS_IN_ONE \
  -D CPACK_PACKAGE_NAME=predictable-pinyin-ibus \
  -D CPACK_PACKAGE_DESCRIPTION_SUMMARY="Predictable Pinyin IBus engine for Linux" \
  -D CPACK_DEBIAN_PACKAGE_DEPENDS="ibus, libibus-1.0-5, ${common_deps}" \
  -D "CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA=${repo_root}/data/debian/postinst;${repo_root}/data/debian/postrm"

# --- Self-extracting installers ---
for framework in ibus; do
  deb="$(ls "predictable-pinyin-${framework}_"*.deb | head -1)"
  installer="install-predictable-pinyin-${framework}.base64.sh"
  cp "${repo_root}/scripts/install-${framework}-deb.sh" "$installer"
  echo "__DEB_PAYLOAD__" >> "$installer"
  base64 "$deb" >> "$installer"
  chmod +x "$installer"
done

echo
ls -lh predictable-pinyin-*.deb *.base64.sh
echo
echo "Done. Distribute the .base64.sh files to target machines and run them."
