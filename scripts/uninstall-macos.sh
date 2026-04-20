#!/usr/bin/env bash
# Remove PredictablePinyin.app from ~/Library/Input Methods/ and purge the
# TIS caches that could otherwise keep a stale entry around.
# Leaves ~/Library/Rime/ user data intact.
#
# No sudo required — everything happens under $HOME.
set -u

app_name="PredictablePinyin"
dest_app="${HOME}/Library/Input Methods/${app_name}.app"
lsregister="/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister"

echo "[1/4] Stopping any running ${app_name} process..."
pkill -9 -x "${app_name}" 2>/dev/null || true

echo "[2/4] Unregistering from LaunchServices..."
if [[ -x "${lsregister}" ]]; then
  "${lsregister}" -u "${dest_app}" 2>/dev/null || true
fi

echo "[3/4] Removing installed bundle..."
rm -rf "${dest_app}"

echo "[4/4] Purging per-user TIS caches and restarting text-input agents..."
cache_root="$(getconf DARWIN_USER_CACHE_DIR 2>/dev/null || true)"
if [[ -n "${cache_root}" ]]; then
  find "${cache_root}" -maxdepth 4 -name 'com.apple.IntlDataCache.le*' -delete 2>/dev/null || true
fi
# cfprefsd + text-input menu/switcher rebuild their caches on relaunch, so
# killing them makes the Settings UI refresh without a log-out.
killall -9 cfprefsd 2>/dev/null || true
killall -9 ControlCenter 2>/dev/null || true
killall -9 TextInputMenuAgent 2>/dev/null || true
killall -9 TextInputSwitcher 2>/dev/null || true

echo
echo "Removed ${dest_app}."
echo "User data at \$HOME/Library/Rime/ was left intact."
