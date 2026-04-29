# Predictable Pinyin

跨平台拼音输入法，强调可预测性，使用户在一段时间后可以不看候选列表快速盲打。

之前不能盲打的主要障碍在于随时间变化的选字列表，使得用1234567890来选字变得难以预测。
该拼音输入法强调用笔画来选字，使其容易预测。尤其对于非高频的字，之前翻页和看字很花时间。

A cross-platform pinyin input method that emphasizes on predictability so users
can quickly type without looking at the candidate list after a while.

Traditional input methods make blind typing hard because the candidate list
keeps changing, and choosing by 1234567890 is unpredictable. Predictable Pinyin
uses stroke input to make character selection stable and predictable, saving
time—especially for characters buried deep in the candidate list.

## Method

1. input pinyin normally (single characters or words/phrases);
   use `'` (apostrophe) to separate syllables when needed (e.g., `ni'ao` for 尼奥
   vs `niao` which only matches single-syllable 鸟)
2. then input `;` to enter the stroke phase (or SPACE to commit the top candidate directly)
3. then input as many strokes as needed using h, s, p, n, d, z
   - h: 横 (horizontal stroke `—`)
   - s: 竖 (vertical stroke `丨`)
   - p: 撇 (left-falling stroke `/`)
   - n: 捺 (right-falling stroke `\`)
   - d: 点 (dot stroke `丶`; 提/挑 rising hooks fold in here too — they're
        hard to distinguish from 点 at a glance, so we pick the easier key)
   - z: 折 (turning/hook stroke `㇠`)
   - each candidate's label shows the **full remaining** stroke sequence
   - press **TAB** to autocomplete all strokes shared by the top 2 candidates
     (e.g., 努 `zphznzp` and 怒 `zphznnznn` share `zphzn` — TAB fills that in)
4. for words, press `;` again to advance stroke matching to the next character
   - e.g., `zhongguo;sz;s` matches 中(sz…) then 国(s…)
   - `;;strokes` skips the first character's strokes entirely
5. input J, K, L, or F to enter candidate selection
   - J: select candidate at +1
   - K: select candidate at +2
   - L: select candidate at +4
   - F: select candidate at +10 (next page)
   - (or press SPACE in step 3 to directly commit the top candidate)
6. continue using J, K, L, F to navigate, BACKSPACE to undo, then SPACE to commit

Step 3-7 can be omitted if there's only one possible candidate (auto-commit).

Throughout the process,
- Ctrl+key and Alt+key combos (e.g., Ctrl+C, Ctrl+V) are passed through to the application
- In IBus, Shift toggles between Chinese and English mode
- the user can use BACKSPACE to undo the last action
- SPACE always commits (top candidate or the currently selected candidate)
- Punctuation keys (`,` `.` `!` `?` `:` `\` `(` `)` `[` `]` `<` `>` `~`)
  commit the current candidate and output the Chinese equivalent
  (`，` `。` `！` `？` `：` `、` `（` `）` `【` `】` `《` `》` `～`)
- `;` from idle outputs Chinese semicolon `；`; during active input it enters/advances strokes
- the hint is shown for what the next key could be (e.g., J, K, L, F), and what they mean
- when the entire input is a valid single syllable (e.g., `niao`), only single-character
  candidates are shown; use apostrophe to split into multi-syllable for words (e.g., `ni'ao`)
- in stroke phase, characters whose strokes exactly match the typed strokes rank above
  characters with only a prefix match, even if the prefix-match char has higher frequency
- when committing a partial match (e.g., single char from multi-syllable input), the
  state machine returns to stroke phase with the remaining pinyin

## Cross-platform support

We support Linux, MacOS, and Windows by utilizing the existing open-source code
from https://github.com/rime.

## Documentation

More detailed project documentation lives in [`doc`](doc/README.md) folder.
LLM agents should read that doc index first and follow its links before starting
larger tasks. LLM agents should also follow all instructions in [LLM agents](#llm-agents) section.

- Docs index: [`doc/README.md`](doc/README.md)
- Linux ibus plan: [`doc/phase-8-ibus-linux-plan.md`](doc/phase-8-ibus-linux-plan.md)
- macOS plan: [`doc/phase-8-macos-plan.md`](doc/phase-8-macos-plan.md)

## Development

### Prerequisites

- **cmake** — for building C++ components
- **clang++** — C++20 compiler from the distro-provided `clang` package
- **pkg-config** — for locating `librime` and ibus development packages
- **librime-dev** — Rime core library headers
- **libibus-1.0-dev** — for the ibus engine (optional)
- **Dart SDK 3.9.2+** — managed via [fvm](https://fvm.app/). Use `fvm dart` instead of `dart` directly. Used by local data-preparation scripts (see [Data preparation scripts](#data-preparation-scripts)).

```bash
# Linux (Ubuntu/Debian) — core + ibus
sudo apt-get update
sudo apt-get install -y cmake pkg-config clang build-essential git \
  librime-dev librime-data-luna-pinyin librime-data-stroke \
  librime-data-pinyin-simp libibus-1.0-dev

# macOS — core + IMK input method
brew install librime pkgconf cmake
```

If cmake fails with "Could not find CMAKE_ROOT", create a local `env.sh`
(gitignored) that wraps cmake to unset the offending variable:

```bash
# env.sh (not checked in — machine-specific)
cmake() {
  env -u OFFENDING_VAR /usr/bin/cmake "$@"
}
```

Then `source env.sh` before running `cmake` or `cpack` commands.

### Building

Build from the project root:

```bash
# source env.sh is only needed for the initial configure step
source env.sh
# only needed the first time, or after CMakeLists.txt / CMake options change
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# normal edit/build loop
./scripts/build.sh
```

This produces:

- `build/predictable_pinyin_cli` — standalone CLI harness backed by `librime`
- `build/pp_core_tests` — Catch2-based unit tests for the core logic
- `build/ibus-engine-predictable-pinyin` — ibus engine executable (if libibus-1.0-dev installed)

### Packaging

Build a self-contained `.deb` package (configure, compile, and package in one step):

```bash
./scripts/build-deb.sh
```

This produces a self-extracting installer and a standalone `.deb` file:

- `install-predictable-pinyin-ibus.base64.sh` — single-file IBus installer
- `predictable-pinyin-ibus_*.deb` — standalone `.deb` file

Install on a target machine — download the `.base64.sh` and run it:

```bash
./install-predictable-pinyin-ibus.base64.sh
```

The script contains the `.deb` embedded inside it. It installs the package,
resolves dependencies, and configures the input method framework.

**Cross-version note:** the `.deb` packages are built for the host OS version.
To target both Ubuntu 20.04 and 24.04, build on each version separately (or
build on 20.04 for forward compatibility).

### Linux Deployment

Framework setup, install, and troubleshooting instructions:

- **ibus**: [`doc/dev-setup-ibus.md`](doc/dev-setup-ibus.md) — `./scripts/install-ibus.sh`

The script builds the project, runs `sudo cmake --install`, deploys the Rime
schema, and restarts the input method framework. Matching uninstall script:

- **ibus**: `./scripts/uninstall-ibus.sh`

### macOS Deployment

Setup, install, and troubleshooting instructions:

- **IMK**: [`doc/dev-setup-macos.md`](doc/dev-setup-macos.md) — `./scripts/install-macos.sh`

The script builds the `.app` bundle, copies it to `~/Library/Input Methods/`
(no sudo), seeds `~/Library/Rime/`, and registers the bundle with TIS so it
appears under System Settings → Keyboard → Input Sources. Matching scripts:

- **IMK**: `./scripts/uninstall-macos.sh`, `./scripts/build-dmg.sh`

Installed data:

- `usr/share/predictable-pinyin/hanzi_db.csv` — frequency database
- `usr/share/predictable-pinyin/stroke.dict.yaml` — stroke dictionary
- `usr/share/predictable-pinyin/pinyin_simp.dict.yaml` — multi-reading pinyin dictionary
- `usr/share/rime-data/predictable_pinyin.schema.yaml` — Rime schema
- `usr/share/icons/hicolor/scalable/apps/predictable-pinyin.svg` — icon (SVG)
- `usr/share/icons/hicolor/48x48/apps/predictable-pinyin.png` — icon (PNG)

### Manual Verification

After installing via either framework, switch to Predictable Pinyin and verify:

1. Type a pinyin syllable, then `;` to enter strokes, then h/s/p/d/n/z, then J/K/L/F to select.
2. Verify the hint text, highlighted candidate, navigation, and SPACE to commit.
   In IBus, hints should appear outside the preedit text and should not be
   inserted into the editor when switching input methods.
3. Type pinyin then SPACE to verify it commits the top candidate directly.
4. Test BACKSPACE at each phase boundary.
5. Type a multi-syllable pinyin (e.g., `zhongguo`), `;` then strokes for the first character,
   `;` again then strokes for the second character. Verify per-character narrowing.
6. Type `,` `.` `!` `?` `:` `\` `(` `)` `[` `]` `<` `>` `~` to verify Chinese punctuation output.
7. Type `;` from idle to verify Chinese semicolon `；` is output.
8. Verify that candidate order follows Rime before and after pressing `;`
   (e.g., `sui` vs `sui;`, and `yue` keeps 月 before polyphone 说).
9. Type `niao` → verify only single chars (鸟, 尿); type `ni'ao` → verify 尼奥 word appears.
10. Type `shi;hs` → verify 十 (exact stroke match) ranks above 事 (prefix match).
11. Type `qianshan;J ` → verify 千 is committed and state returns to stroke phase with `shan`.

### Testing

Use the CLI to exercise the state machine without modifying your live ibus session.
Pass a Rime user-data directory as a template via `PREDICTABLE_PINYIN_USER_DATA_DIR`;
the CLI clones it into a temporary directory for that run and cleans it up
automatically, so no manual `rm` is needed between tests.

The CLI reads data files from `data/raw/` by default (relative to the working
directory). Override with environment variables:

- `PREDICTABLE_PINYIN_STROKE_DICT_PATH` — stroke dictionary (default: `data/raw/stroke.dict.yaml`)
- `PREDICTABLE_PINYIN_HANZI_DB_PATH` — frequency database (default: `data/raw/hanzi_db.csv`)
- `PREDICTABLE_PINYIN_PINYIN_DICT_PATH` — supplementary pinyin dictionary for multi-reading characters (default: `data/raw/pinyin_simp.dict.yaml`)

```bash
# selection navigation / commit (run from project root)
# ; enters strokes, J enters selection (+1), K skips +2, D undoes, SPACE commits
PREDICTABLE_PINYIN_USER_DATA_DIR="$HOME/.config/ibus/rime" \
  ./build/predictable_pinyin_cli n i ';' J K D '<space>'

# pinyin → ; → stroke filtering → SPACE commits top candidate
PREDICTABLE_PINYIN_USER_DATA_DIR="$HOME/.config/ibus/rime" \
  ./build/predictable_pinyin_cli z h o n g ';' s '<space>'
```

Set `PREDICTABLE_PINYIN_REUSE_USER_DATA_DIR=1` only if you intentionally want the CLI
to use the provided directory directly instead of creating an isolated temp copy.

Run the unit tests from the project root:

```bash
# after the first configure, this is the normal loop
./scripts/build.sh
cd build
ctest --output-on-failure
```

The current unit tests cover:

- canonical pinyin loading and auto-end detection in `pinyin_trie`
- state transitions (no auto-end, SPACE-only pinyin→stroke), J/K/L/F/D from
  stroke entering selection, undo/backspace, and selection commit flow in
  `predictable_state_machine`
- stroke dict loading, per-character prefix filtering, multi-stroke narrowing,
  next-stroke lookup, SplitUtf8, and unknown-char handling in `stroke_filter`
- pinyin-reading validation, Rime order preservation, and polyphone ordering
  regressions in `frequency_sorter`
- end-to-end integration tests: full flow (pinyin → stroke → select via J →
  commit), SPACE/`;` commit shortcuts, d-as-stroke-key, pinyin prefix filtering
  during typing and exact filtering after `;`, auto-commit with single candidate,
  multi-reading characters (多音字), backspace at all state boundaries,
  deterministic ordering, data-loading performance, idle key-handling regression,
  candidate label correctness in stroke/selecting phases, tone-stripping
- word input tests: multi-char candidates from pinyin, per-character stroke
  narrowing via multiple `;`, skip-first-char (`;;`), backspace across segment
  boundaries, Rime ordering in stroke phase, preedit segment display, stroke
  hints for current character position, J/K/L/F selection among word candidates
- multi-syllable pinyin tests: single-char preservation, stroke filtering,
  virtual word composition from single characters (e.g., `yuqian;ddz;ddz` →
  宇骞 even when Rime doesn't have the word)
- punctuation tests: all 13 punctuation keys from idle produce Chinese
  equivalents, punctuation from active phases commits + outputs Chinese,
  `;` from idle outputs `；` but from pinyin/stroke retains its stroke role
- apostrophe tests: single-syllable pinyin filters out multi-char candidates,
  apostrophe enables word candidates, multi-syllable without apostrophe still
  shows words
- stroke priority tests: exact stroke match ranks above prefix-only match
- partial commit tests: committing a partial match returns to stroke phase
  with remaining pinyin, full word commit returns to idle
- multi-reading (多音字) tests: characters with multiple pinyin readings
  (e.g., 的 as de/di, 沈 as chen/shen, 重 as zhong/chong) are accessible
  via all their pronunciations, both in pinyin prefix and exact-match phases

The test binary uses [Catch2](https://github.com/catchorg/Catch2), and CTest
discovers each test case individually so the output shows human-readable test
titles instead of a single opaque test binary status.

### clangd / IDE navigation

`cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` generates
`build/compile_commands.json`, which clangd uses to understand include paths,
compiler flags, and jump-to-definition targets.

If your IDE does not pick it up automatically, use one of these setups:

```bash
# option 1: point clangd at the build directory
clangd --compile-commands-dir=build

# option 2: expose the database at the repo root
ln -sf build/compile_commands.json compile_commands.json
```

For Cursor/VS Code clangd settings, the equivalent argument is:

```json
{
  "clangd.arguments": ["--compile-commands-dir=build"]
}
```

### Data files

Data files in `data/raw/` are downloaded automatically from upstream repositories
by `scripts/download-data.sh` (called by `scripts/build.sh`). SHA256 checksums
are verified after download. Only `pinyin_simp.prism.txt` is checked into git.

| File | Source | Description |
|------|--------|-------------|
| `data/raw/hanzi_db.csv` | [ruddfawcett/hanziDB.csv](https://github.com/ruddfawcett/hanziDB.csv) (MIT) | Character frequency database (sorted by rank) |
| `data/raw/stroke.dict.yaml` | Generated from [theajack/cnchar](https://github.com/theajack/cnchar) (MIT) by `scripts/bin/build_stroke_map.dart` | Character → stroke sequence (h/s/p/n/d/z) |
| `data/raw/pinyin_simp.dict.yaml` | [rime/rime-pinyin-simp](https://github.com/rime/rime-pinyin-simp) (Apache-2.0) | Pinyin → character mapping with multi-reading support (多音字) |
| `data/raw/pinyin_simp.prism.txt` | Rime deployment artifact (checked in) | Canonical pinyin syllable list |

To re-download or refresh data files manually: `bash scripts/download-data.sh`

### Data preparation scripts

Local generator scripts that **transform** upstream data into files under
`data/raw/` live under `scripts/bin/*.dart` and run via `fvm dart run`.
Install / build / download orchestration scripts remain Bash (those are glue,
not transformations).

```bash
cd scripts
fvm dart pub get
fvm dart run bin/build_stroke_map.dart   # → ../data/raw/stroke.dict.yaml
```

Generators pin their upstream source to a specific commit SHA and verify
SHA256 of the downloaded inputs. They no-op when the output file already
exists, so re-running is cheap; delete the output to force regeneration.

### LLM agents

#### 1. Ask approval early in one batch

For LLM agents (e.g., Opus 4.6), please list all possible tools and approvals needed before any long
running tasks. Dry run with some quick dummy commands to add as many of them to the whitelist as
possible. This ensures that the agents can run as long as needed without unnecessary interruptions.

#### 2. Write self-contained test to drive development

Don't write tests that require `kill` or `rm` during the loop. Write self-contained tests so the
agents can iterate without constantly asking for approvals of dangerous commands like `kill` or
`rm`.

#### 3. Minimize complexity, including test code

Try your best to simplify the code to minimize complexity, reduce repeated code or code that other
libs can already provide.

Even for test code, minimize its complexity. Try to cover as many edge cases or code as possible by
the minimum number of testing code. Meanwhile, ensure that the test code is readable.

#### 4. Document how run tools and tests, and how to maintain them

Such instructions are helpful for both humans and LLM agents.

#### 5. Update docs when code changes

Put docs in local folder with the code, and ensure that the docs are updated when the code changes.

#### 6. Run full suite of tests before committing

Before finishing a batch of changes, run the full suite of tests according to the README.md files
across the project.

#### 7. Follow best practices in project docs

For exampole, if a README.md documents that a build can be accelerated by concurrency using a
specific build script, use it to accelerate the build and agent iteration.

#### 8. Use Dart for local data-preparation scripts

Generator scripts that transform upstream data into files under `data/raw/` go in
`scripts/bin/*.dart` and run via `fvm dart run`. Install / build / download
orchestration (e.g., `scripts/build.sh`, `scripts/download-data.sh`,
`scripts/install-ibus.sh`) remains Bash — those are glue, not transformations.
See [Data preparation scripts](#data-preparation-scripts).
