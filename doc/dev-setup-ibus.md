# ibus Development Setup (Linux)

For prerequisites, building, and testing, see the root [README](../README.md#development).
This page covers only ibus-specific setup.

## Extra Package

```bash
sudo apt-get install -y ibus libibus-1.0-dev
```

Verify headers: `pkg-config --cflags --libs ibus-1.0`

## Engine Model

ibus engines are **standalone executables**, not shared libraries. The engine
process communicates with the ibus daemon over D-Bus. This means:

- The build produces `ibus-engine-predictable-pinyin` (an executable)
- A component XML file (`predictable-pinyin.xml`) tells ibus how to launch it
- The Rime user data directory is `~/.config/ibus/rime/`
- The icon path is an absolute file path in the XML, not a theme lookup

## Install and Deploy

```bash
./scripts/install-ibus.sh
```

The script builds the project, runs `sudo cmake --install`, deploys the Rime
schema to `~/.config/ibus/rime/`, runs `ibus write-cache` to regenerate the
component cache, and `ibus restart` to pick up the new engine.

## Uninstall

```bash
./scripts/uninstall-ibus.sh         # remove installed files & restart ibus
```

The script detects whether the deb package or a cmake install is present and
removes accordingly. It also removes Predictable Pinyin from GNOME input sources
if configured. User data in `~/.config/ibus/rime/` is left intact.

## Installed Files

| Installed file | Purpose |
|----------------|---------|
| `usr/libexec/ibus-engine-predictable-pinyin` | Engine executable |
| `usr/share/ibus/component/predictable-pinyin.xml` | Component descriptor |
| `usr/share/predictable-pinyin/icons/predictable-pinyin.svg` | Icon (absolute path in XML) |
| (shared) `usr/share/predictable-pinyin/`, `usr/share/rime-data/` | See [README](../README.md#linux-deployment) |

## Manual Verification

1. Open GNOME Settings → Region & Language → Input Sources, or run
   `ibus engine predictable-pinyin`.
2. Follow [common verification steps](../README.md#manual-verification).

## Troubleshooting

- **Engine not listed**: Run `ibus write-cache` then `ibus restart`. Check that
  `predictable-pinyin.xml` exists in `/usr/share/ibus/component/`.
- **Engine crashes on start**: Run the engine directly for debug output:
  `/usr/libexec/ibus-engine-predictable-pinyin --ibus`
- **Schema not deployed**: Run `rime_deployer --build ~/.config/ibus/rime /usr/share/rime-data`
  manually, then restart ibus.
- **Icon not showing**: Verify the `<icon>` path in `predictable-pinyin.xml` points
  to an existing file.
