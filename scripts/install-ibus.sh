#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="${repo_root}/build"
user_data_dir="${PREDICTABLE_PINYIN_USER_DATA_DIR:-$HOME/.config/ibus/rime}"
shared_data_dir="${PREDICTABLE_PINYIN_SHARED_DATA_DIR:-/usr/share/rime-data}"

if [[ -f "${repo_root}/env.sh" ]]; then
  # shellcheck disable=SC1091
  source "${repo_root}/env.sh"
fi

echo "Configuring CMake build directory..."
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_INSTALL_PREFIX=/usr

echo "Building project..."
"${repo_root}/scripts/build.sh"

echo "Installing plugin and data files..."
sudo cmake --install build --prefix /usr

mkdir -p "${user_data_dir}"
need_deploy=false
if [[ ! -f "${user_data_dir}/default.custom.yaml" ]]; then
  echo "Creating default.custom.yaml for ibus Rime..."
  cat > "${user_data_dir}/default.custom.yaml" <<'YAML'
patch:
  schema_list:
    - schema: predictable_pinyin
    - schema: luna_pinyin
    - schema: pinyin_simp
YAML
  need_deploy=true
elif grep -q 'predictive_pinyin' "${user_data_dir}/default.custom.yaml"; then
  echo "Migrating default.custom.yaml from predictive_pinyin → predictable_pinyin..."
  sed -i 's/predictive_pinyin/predictable_pinyin/g' "${user_data_dir}/default.custom.yaml"
  rm -rf "${user_data_dir}/build"
  need_deploy=true
fi
if [[ "$need_deploy" == true ]] || [[ ! -d "${user_data_dir}/build" ]]; then
  if command -v rime_deployer >/dev/null 2>&1; then
    echo "Deploying Rime schema into ${user_data_dir}/build ..."
    rime_deployer --build "${user_data_dir}" "${shared_data_dir}"
  fi
fi

echo "Updating ibus component cache..."
ibus write-cache

if command -v ibus >/dev/null 2>&1; then
  echo "Restarting ibus..."
  ibus restart || true
fi

cat <<EOF

Installed Predictable Pinyin for ibus.

To add the input method:
  - GNOME: Settings → Region & Language → Input Sources → + → Chinese → Predictable Pinyin
  - Command line: ibus engine predictable-pinyin

If changes do not take effect, run:
  ibus write-cache && ibus restart
EOF
