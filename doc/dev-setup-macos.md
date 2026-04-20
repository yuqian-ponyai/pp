# macOS Development Setup

For prerequisites, building, and testing, see the root
[README](../README.md#development). This page covers only macOS-specific
setup.

## Extra Packages

```bash
brew install librime pkgconf
```

`librime` provides the Rime engine + the `rime_deployer` CLI. `pkgconf`
is the pkg-config replacement CMake uses to discover librime.

If `pkg-config` isn't already on your PATH after `brew install pkgconf`,
either source `env.sh` (created automatically from the template below)
or set `PKG_CONFIG_PATH` manually.

### `env.sh` (machine-specific, gitignored)

`scripts/build.sh` sources `env.sh` unconditionally. Create one at the
repo root:

```bash
# env.sh (gitignored)
if command -v brew >/dev/null 2>&1; then
  export PKG_CONFIG_PATH="$(brew --prefix librime)/lib/pkgconfig:$(brew --prefix pkgconf)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  export PATH="$(brew --prefix pkgconf)/bin:$PATH"
fi
```

## Engine Model

macOS input methods are `.app` bundles. We install to
`~/Library/Input Methods/` — the user-writable location, which does not
require `sudo` or an Apple Developer Program enrollment. macOS still
expects the bundle to be launched and registered via
`TISRegisterInputSource()` before it appears in System Settings.

- Build produces `build/PredictablePinyin.app` (a CMake `MACOSX_BUNDLE`
  target)
- `Info.plist` keys register the `IMKInputController` subclass
- Icon (`PredictablePinyin.icns`) is generated from
  `data/icons/predictable-pinyin.png` by `scripts/build-icns.sh`
- Rime shared data ships inside `Contents/SharedSupport/`
- Rime user data lives at `~/Library/Rime/`
- librime is loaded from Homebrew at runtime; the bundle links against
  `$(brew --prefix librime)/lib/librime.1.dylib` and users are expected
  to have run `brew install librime` themselves. No embedded dylib,
  no self-contained relocation.

This matches the
[fcitx5-macos-installer](https://github.com/fcitx-contrib/fcitx5-macos-installer)
reference pattern: user-dir install, ad-hoc codesign, explicit
`TISRegisterInputSource` call. No Developer ID certificate or
notarization is required because the bundle is not placed under
`/Library/Input Methods/`.

## Install and Deploy

```bash
./scripts/install-macos.sh
```

What it does:

1. `scripts/build-icns.sh` regenerates the `.icns` (no-op if up-to-date)
2. `cmake -B build -DCMAKE_BUILD_TYPE=Release …`
3. `./scripts/build.sh` builds `build/PredictablePinyin.app`
4. Copies the bundle to `~/Library/Input Methods/PredictablePinyin.app`
   and ad-hoc codesigns it
5. Seeds `~/Library/Rime/default.custom.yaml` with the schema list
6. Runs `rime_deployer --build` against the bundle's `SharedSupport/`
7. `open`s the bundle once so IMKServer binds the connection
8. Calls `PredictablePinyin --register-input-source`, which invokes
   `TISRegisterInputSource(bundleURL)`. Without this call the bundle
   never shows up in System Settings — macOS only scans Input Methods
   directories at login and requires the explicit API call for freshly
   installed bundles.
9. Calls `PredictablePinyin --enable-input-source` so the registered
   mode is enabled in the user's TIS prefs.

The bundle's `main()` sets
`NSApp.activationPolicy = .accessory` so the IMK server process stays
alive as a background-only input method instead of being auto-terminated
for having no windows.

After install, add the input method:

- System Settings → Keyboard → Input Sources → **+** → Chinese
  (Simplified) → **Predictable Pinyin**
- Switch via the menu-bar input-source picker or Ctrl+Space

## Uninstall

```bash
./scripts/uninstall-macos.sh
```

Removes `~/Library/Input Methods/PredictablePinyin.app`, unregisters it
from LaunchServices, and purges the per-user TIS caches so the Settings
UI reloads cleanly. Leaves `~/Library/Rime/` intact.

## Packaging a `.dmg`

```bash
./scripts/build-dmg.sh       # → build/PredictablePinyin.dmg
```

The bundle inside the dmg is ad-hoc signed. Recipients mount the dmg,
run the install script (or drag the bundle into their own
`~/Library/Input Methods/` and call `--register-input-source`
themselves), and `brew install librime` for the runtime.

## Installed Files

| Installed file | Purpose |
|----------------|---------|
| `~/Library/Input Methods/PredictablePinyin.app/Contents/MacOS/PredictablePinyin` | Engine executable |
| `~/Library/Input Methods/PredictablePinyin.app/Contents/Info.plist` | IMK component descriptor |
| `~/Library/Input Methods/PredictablePinyin.app/Contents/Resources/PredictablePinyin.icns` | Icon |
| `~/Library/Input Methods/PredictablePinyin.app/Contents/SharedSupport/{stroke.dict.yaml,hanzi_db.csv,pinyin_simp.{prism.txt,dict.yaml},predictable_pinyin.schema.yaml}` | Data files |
| `~/Library/Rime/default.custom.yaml` | User schema list |
| `~/Library/Rime/build/` | Rime deployer output |

## Manual Verification

1. Run `./scripts/install-macos.sh`.
2. Follow the [common verification steps](../README.md#manual-verification).
3. Verify Shift alone toggles CN/EN, and Ctrl+C / Cmd+C pass through to
   the host app.

## Troubleshooting

- **Bundle doesn't appear in System Settings → Input Sources.** Re-run
  `./scripts/install-macos.sh`. The install script's final output
  should include `registered im.predictablepinyin.inputmethod.PredictablePinyin`
  and `enabled [TISCategoryKeyboardInputSource] …`. If not, query TIS
  directly:
  ```bash
  swift - <<'EOF'
  import Carbon
  let f: [String: Any] = [kTISPropertyBundleID as String:
      "im.predictablepinyin.inputmethod.PredictablePinyin"]
  print(TISCreateInputSourceList(f as CFDictionary, true)
      ?.takeRetainedValue() as Any)
  EOF
  ```
  An empty result means the register call never ran; `nil` means the
  bundle identifier in TIS's filter didn't match (Info.plist drift).
- **Bundle appears, but candidates don't show up.** Confirm
  `rime_deployer` ran without error:
  ```bash
  "$(brew --prefix librime)/bin/rime_deployer" --build \
    ~/Library/Rime \
    ~/Library/Input\ Methods/PredictablePinyin.app/Contents/SharedSupport
  ```
- **Engine crashes on launch.** Tail the system log for our bundle id or
  run the binary directly to see what happens on stdout/stderr:
  ```bash
  log stream --predicate 'subsystem == "im.predictablepinyin.inputmethod.PredictablePinyin"'
  ~/Library/Input\ Methods/PredictablePinyin.app/Contents/MacOS/PredictablePinyin
  ```
- **`Library not loaded: …/librime.1.dylib`.** The Homebrew librime
  prefix changed (e.g. version bump). Re-run `./scripts/install-macos.sh`
  so CMake re-links against the current prefix.
- **`pkg-config: not found` during build.** `brew install pkgconf`, then
  source `env.sh` (see above).
- **`library 'rime' not found` at link time.** `PKG_CONFIG_PATH` is not
  pointing at Homebrew's librime. Source `env.sh`.
