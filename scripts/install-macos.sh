#!/usr/bin/env bash
# Build PredictablePinyin.app and install it to ~/Library/Input Methods/.
#
# No sudo, no Apple Developer Program required. Ad-hoc codesigning is
# sufficient because:
#   - The bundle lives under the user's own Library, not /Library (no root
#     ownership required).
#   - On modern macOS, new input-method bundles only appear in System
#     Settings after an explicit TISRegisterInputSource() call, which the
#     bundle itself makes via `PredictablePinyin --register-input-source`.
#     That path does not require Developer ID signing or notarization.
# This mirrors the install flow used by the fcitx5-macos-installer reference
# (see doc/dev-setup-macos.md).
#
# Prerequisites:
#   brew install librime pkgconf
#
# After install, add "Predictable Pinyin" under
#   System Settings → Keyboard → Input Sources → + → Chinese (Simplified).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

app_name="PredictablePinyin"
build_dir="${repo_root}/build"
src_app="${build_dir}/${app_name}.app"
dest_app="${HOME}/Library/Input Methods/${app_name}.app"
user_data="${PREDICTABLE_PINYIN_USER_DATA_DIR:-$HOME/Library/Rime}"

# Ensure pkg-config can find librime when invoked from this shell.
if command -v brew >/dev/null 2>&1; then
  export PKG_CONFIG_PATH="$(brew --prefix librime)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
fi

"${repo_root}/scripts/build-icns.sh"

# Re-run configure: no-op on unchanged builds, but picks up CMakeLists edits.
cmake -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

"${repo_root}/scripts/build.sh"

if [[ ! -d "${src_app}" ]]; then
  echo "Build did not produce ${src_app}" >&2
  exit 1
fi

echo "Installing ${src_app} → ${dest_app}"
mkdir -p "$(dirname "${dest_app}")"
pkill -9 -x "${app_name}" 2>/dev/null || true

# Wipe any stale install from this machine's history (prior iterations shipped
# under a different bundle id and/or /Library/Input Methods/). Leaving those
# around confuses TIS and can shadow the freshly installed bundle.
lsregister="/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister"
for stale in \
  "${dest_app}" \
  "/Library/Input Methods/${app_name}.app"; do
  if [[ -e "${stale}" ]]; then
    [[ -x "${lsregister}" ]] && "${lsregister}" -u "${stale}" 2>/dev/null || true
    # /Library/ needs sudo; skip if not writable so we stay sudo-free.
    rm -rf "${stale}" 2>/dev/null || true
  fi
done
# Purge per-user TIS caches (rebuilt on demand).
cache_root="$(getconf DARWIN_USER_CACHE_DIR 2>/dev/null || true)"
if [[ -n "${cache_root}" ]]; then
  find "${cache_root}" -maxdepth 4 -name 'com.apple.IntlDataCache.le*' -delete 2>/dev/null || true
fi

cp -R "${src_app}" "${dest_app}"
# Ad-hoc sign so macOS accepts the locally-built bundle.
codesign --force --deep --sign - "${dest_app}"

mkdir -p "${user_data}"
# Earlier iterations of this script wrote a `default.custom.yaml` that
# referenced schemas (luna_pinyin, pinyin_simp) we don't ship. That patch
# never applied cleanly and hid the real problem. Remove it if present so
# Rime falls back to the canonical default.yaml we ship in SharedSupport.
if [[ -f "${user_data}/default.custom.yaml" ]]; then
  if grep -q 'luna_pinyin\|pinyin_simp' "${user_data}/default.custom.yaml"; then
    rm -f "${user_data}/default.custom.yaml"
  fi
fi

# Wipe stale deploy artifacts so rime_deployer / Rime auto-deploy rebuilds
# against the bundle we just installed. Without this, an older empty build/
# cache makes Rime believe everything is up to date and produce no
# candidates.
rm -rf "${user_data}/build"

# Pre-deploy the Rime schema so first-run is fast and any errors surface
# now (not silently inside the IM process).
if command -v brew >/dev/null 2>&1; then
  rime_deployer="$(brew --prefix librime)/bin/rime_deployer"
  if [[ -x "${rime_deployer}" ]]; then
    shared="${dest_app}/Contents/SharedSupport"
    echo "Running rime_deployer --build ${user_data} ${shared}"
    "${rime_deployer}" --build "${user_data}" "${shared}"
  fi
fi

# Restart the text-input daemons so they re-scan Input Methods with a clean
# cache. Matches the cleanup pattern in ~/playground/imk/cleanup.sh.
killall -9 cfprefsd 2>/dev/null || true
killall -9 TextInputMenuAgent 2>/dev/null || true
killall -9 TextInputSwitcher 2>/dev/null || true

# Launch the bundle once so IMKServer binds the connection name; then tell
# TIS about it and enable the registered source(s).
/usr/bin/open "${dest_app}"
sleep 1
dest_bin="${dest_app}/Contents/MacOS/${app_name}"
"${dest_bin}" --register-input-source
"${dest_bin}" --enable-input-source || true

cat <<EOF

Installed Predictable Pinyin at:
  ${dest_app}

Next steps:
  1. Open System Settings → Keyboard → Input Sources → + → Chinese (Simplified)
     → "Predictable Pinyin" (if it's not already selected).
  2. Switch to it via the menu-bar input-source picker or Ctrl+Space.
EOF
