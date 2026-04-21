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
# Skipping any of these can leave System Settings → Keyboard → Input Sources
# showing a stale ghost of the IM (selectable but broken) or graying out a
# re-install, which is the failure mode that required manually running these
# killalls after the previous uninstall.
cache_root="$(getconf DARWIN_USER_CACHE_DIR 2>/dev/null || true)"
if [[ -n "${cache_root}" ]]; then
  # Broaden from `.le*` to `*` so we also catch the big-endian and
  # suffix-less variants macOS has shipped across versions.
  find "${cache_root}" -maxdepth 4 -name 'com.apple.IntlDataCache*' -delete 2>/dev/null || true
fi
# HIToolbox keeps a parallel cache of registered input sources under
# ~/Library/Caches; nuke it so the daemons rebuild from the on-disk plist
# rather than from a cached view that still remembers our bundle.
rm -rf "${HOME}/Library/Caches/com.apple.HIToolbox" 2>/dev/null || true
# cfprefsd + the text-input agents rebuild their caches on relaunch, so
# killing them makes the Settings UI refresh without a log-out.
killall -9 cfprefsd 2>/dev/null || true
killall -9 ControlCenter 2>/dev/null || true
killall -9 TextInputMenuAgent 2>/dev/null || true
killall -9 TextInputSwitcher 2>/dev/null || true
# TextInputUIService renders the Input Sources picker in System Settings on
# recent macOS; killing it forces Settings to re-enumerate on next open.
killall -9 TextInputUIService 2>/dev/null || true

echo
echo "Removed ${dest_app}."
echo "User data at \$HOME/Library/Rime/ was left intact."
