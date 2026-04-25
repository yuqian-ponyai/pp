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

macOS input methods are `.app` bundles installed under
`~/Library/Input Methods/` (user-writable, no sudo / Developer Program
required). librime is loaded from Homebrew at runtime — no embedded
dylib. See [phase-8-macos-plan.md](./phase-8-macos-plan.md) for the
full design.

## Install and Deploy

```bash
./scripts/install-macos.sh
```

The script builds the `.app`, ad-hoc codesigns it, copies it to
`~/Library/Input Methods/`, seeds `~/Library/Rime/default.custom.yaml`,
runs `rime_deployer --build` against the bundle's `SharedSupport/`,
and calls `TISRegisterInputSource` / `TISEnableInputSource` so the
input source appears in System Settings without a logout.

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
