#!/usr/bin/env bash
set -euo pipefail

echo "Uninstalling Predictable Pinyin for ibus..."

# Prefer dpkg removal if the deb package is installed
if dpkg -s predictable-pinyin-ibus &>/dev/null; then
  echo "Removing deb package predictable-pinyin-ibus..."
  sudo dpkg -r predictable-pinyin-ibus
  deb_removed=1
fi
if dpkg -s predictable-pinyin-common &>/dev/null; then
  echo "Removing deb package predictable-pinyin-common..."
  sudo dpkg -r predictable-pinyin-common
  deb_removed=1
fi

if [[ "${deb_removed:-}" != 1 ]]; then
  # Remove cmake-installed files (common + ibus-specific)
  files=(
    /usr/bin/predictable_pinyin_cli
    /usr/share/rime-data/predictable_pinyin.schema.yaml
    /usr/share/icons/hicolor/scalable/apps/predictable-pinyin.svg
    /usr/share/icons/hicolor/scalable/status/predictable-pinyin.svg
    /usr/share/icons/hicolor/48x48/apps/predictable-pinyin.png
    /usr/libexec/ibus-engine-predictable-pinyin
    /usr/share/ibus/component/predictable-pinyin.xml
  )

  for f in "${files[@]}"; do
    if [[ -f "$f" ]]; then
      echo "  rm $f"
      sudo rm -f "$f"
    fi
  done

  if [[ -d /usr/share/predictable-pinyin ]]; then
    echo "  rm -r /usr/share/predictable-pinyin"
    sudo rm -rf /usr/share/predictable-pinyin
  fi
fi

echo "Updating ibus component cache..."
ibus write-cache 2>/dev/null || true

# Remove from GNOME input sources if present
if gsettings get org.gnome.desktop.input-sources sources &>/dev/null; then
  current="$(gsettings get org.gnome.desktop.input-sources sources)"
  if echo "$current" | grep -q "predictable-pinyin"; then
    new="$(echo "$current" | sed "s/, *('ibus', 'predictable-pinyin')//; s/('ibus', 'predictable-pinyin'), *//")"
    gsettings set org.gnome.desktop.input-sources sources "$new"
    echo "Removed Predictable Pinyin from GNOME input sources."
  fi
fi

if command -v ibus >/dev/null 2>&1; then
  echo "Restarting ibus..."
  ibus restart || true
fi

cat <<EOF

Uninstalled Predictable Pinyin for ibus.

User data in ~/.config/ibus/rime/ was left intact.
To remove it:  rm -rf ~/.config/ibus/rime/

If changes do not take effect, run:
  ibus write-cache && ibus restart
EOF
