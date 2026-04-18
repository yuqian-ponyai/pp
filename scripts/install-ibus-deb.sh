#!/usr/bin/env bash
# Install Predictable Pinyin for IBus.
#
# Standalone:       ./install-ibus-deb.sh predictable-pinyin-ibus_*.deb
# Self-extracting:  ./install-predictable-pinyin.sh  (deb embedded in script)
set -euo pipefail

deb="${1:-}"

# If no argument, try to extract embedded payload
if [[ -z "$deb" ]]; then
  payload_line=$(grep -an '^__DEB_PAYLOAD__$' "$0" | tail -1 | cut -d: -f1)
  if [[ -n "$payload_line" ]]; then
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT
    deb="$tmpdir/predictable-pinyin-ibus.deb"
    tail -n +"$((payload_line + 1))" "$0" | base64 -d > "$deb"
  fi
fi

# Fall back to glob in current directory
if [[ -z "$deb" || ! -f "$deb" ]]; then
  deb="$(ls predictable-pinyin-ibus_*.deb 2>/dev/null | head -1)" || true
fi
if [[ -z "$deb" || ! -f "$deb" ]]; then
  echo "Usage: $0 [predictable-pinyin-ibus_*.deb]" >&2
  exit 1
fi

echo "Installing $deb ..."
sudo dpkg -i "$deb" || true
sudo apt --fix-broken install -y

echo "Restarting IBus daemon ..."
ibus-daemon -drx
sleep 2

# Try adding to GNOME input sources (no-op if not on GNOME)
if gsettings get org.gnome.desktop.input-sources sources &>/dev/null; then
  current="$(gsettings get org.gnome.desktop.input-sources sources)"
  if echo "$current" | grep -q "predictable-pinyin"; then
    echo "Predictable Pinyin already in GNOME input sources."
  else
    if [[ "$current" == "@a(ss) []" || "$current" == "[]" ]]; then
      gsettings set org.gnome.desktop.input-sources sources \
        "[('xkb', 'us'), ('ibus', 'predictable-pinyin')]"
    else
      new="${current%]*}, ('ibus', 'predictable-pinyin')]"
      gsettings set org.gnome.desktop.input-sources sources "$new"
    fi
    echo "Added Predictable Pinyin to GNOME input sources."
  fi
fi

echo "Activating Predictable Pinyin ..."
ibus engine predictable-pinyin 2>/dev/null || true

echo
echo "Done! Predictable Pinyin is ready."
echo
echo "To switch input methods:"
echo "  GNOME: Super+Space"
echo "  CLI:   ibus engine predictable-pinyin"

exit 0
