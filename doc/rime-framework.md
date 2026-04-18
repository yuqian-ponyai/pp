# Rime IME Framework

Research on the Rime input method engine — the cross-platform foundation for Predictable Pinyin.

## Overview

Rime (中州韻輸入法引擎) is a modular, extensible input method engine written in C++17. It is
not an input method itself, but an abstract framework from which specific input methods can be
instantiated via configuration files (YAML schemas).

- **Source**: https://github.com/rime/librime
- **Schema design guide**: https://rimeinn.github.io/rime/schema-design.html

## Cross-Platform Clients

Rime provides platform-specific frontends that all share the same `librime` core:

| Platform | Client Name | Language | Repo |
|----------|------------|----------|------|
| macOS    | Squirrel (鼠鬚管) | Swift | [rime/squirrel](https://github.com/rime/squirrel) |
| Windows  | Weasel (小狼毫)   | C++   | [rime/weasel](https://github.com/rime/weasel) |
| Linux    | ibus-rime (中州韻) | C     | [rime/ibus-rime](https://github.com/rime/ibus-rime) |

All clients read from the same user data folder and share schema/dictionary formats.

### Data Folder Locations

| Platform | Shared Data | User Data |
|----------|------------|-----------|
| macOS | `/Library/Input Methods/Squirrel.app/Contents/SharedSupport/` | `~/Library/Rime/` |
| Windows | `<install_dir>\data` | `%APPDATA%\Rime` |
| Linux (ibus) | `/usr/share/rime-data/` | `~/.config/ibus/rime/` |

## Engine Architecture

The engine processes keystrokes through a pipeline of four component types:

```
Keystroke → Processors → Segmentors → Translators → Filters → Candidates
```

### 1. Processors

Handle raw key events. Each processor in the list gets a chance to accept, reject,
or pass on the key. Common processors:

- `ascii_composer` — handles ASCII/Chinese mode switching
- `speller` — accepts character keys, builds the input code
- `punctuator` — maps single keys to punctuation symbols
- `selector` — handles number keys for candidate selection, page up/down
- `key_binder` — conditionally rebinds keys (e.g., comma/period for paging)
- `express_editor` — handles space (confirm), enter (commit raw), backspace
- `fluid_editor` — alternative editor for sentence-flow input
- `chord_composer` — for chord-typing (multiple keys pressed simultaneously)

### 2. Segmentors

Split the input code into typed segments. Each segment gets tagged (e.g., `abc`,
`punct`, `number`).

- `abc_segmentor` — tags normal alphabetic input
- `punct_segmentor` — tags punctuation
- `fallback_segmentor` — catches everything else

### 3. Translators

Convert tagged code segments into candidate lists. The two main types:

- `script_translator` (aka `r10n_translator`) — for phonetic input (pinyin, etc.)
  Uses a syllable table to split input into valid syllable combinations. Supports
  fuzzy pinyin, abbreviated pinyin, smart phrase composition.
- `table_translator` — for code-table input (wubi, cangjie, stroke, etc.)
  Direct code→character mapping. Supports fixed-length encoding, top-up commit.

Other translators:
- `punct_translator` — converts punctuation segments
- `echo_translator` — echoes the raw input code as a fallback candidate
- `reverse_lookup_translator` — looks up characters using an alternative encoding

### 4. Filters

Post-process the candidate list before display:

- `simplifier` — traditional↔simplified conversion (via OpenCC)
- `uniquifier` — removes duplicate candidates
- `lua_filter` — custom Lua script for arbitrary candidate manipulation

## Schema Definition

A schema is a YAML file named `<schema_id>.schema.yaml`. Minimal structure:

```yaml
schema:
  schema_id: predictable_pinyin
  name: "Predictable Pinyin"
  version: "0.1"
  author:
    - "Author Name"
  description: |
    Cross-platform pinyin input with stroke disambiguation.
  dependencies:
    - stroke          # Can declare dependencies on other schemas

engine:
  processors: [...]
  segmentors: [...]
  translators: [...]
  filters: [...]

translator:
  dictionary: predictable_pinyin    # Name of the .dict.yaml file
```

## Dictionary Format

Dictionaries are `.dict.yaml` files with a YAML header followed by a TSV code table:

```yaml
---
name: my_dict
version: "1.0"
sort: by_weight              # or "original"
use_preset_vocabulary: true  # import Rime's built-in word list (八股文)
...

# character<TAB>encoding<TAB>optional_weight
的	de	99%
的	di	1%
你	ni
我	wo
你好              # word encoding inferred from single-char encodings
```

## Extension Mechanisms

Rime supports extending the engine through several mechanisms:

### C++ Plugins (recommended for this project)

Custom processors, translators, segmentors, and filters can be built as C++ shared
libraries and loaded by librime. This gives full access to the engine internals.

- **Example**: [librime-sample](https://github.com/rime/librime-sample)
- Plugins are placed in the `plugins/` directory of the librime build tree
- Built using CMake with librime as a dependency
- Register components via module initialization functions

For this project, we will **fork Rime's client code** (Squirrel, Weasel, or
ibus-rime) and add custom C++ processors/filters directly. This gives us full
control over the input pipeline without external runtime dependencies.

### Lua Scripting (librime-lua)

Rime supports Lua scripting for lighter customizations. Not used for this project.

- **Reference**: https://rimeinn.github.io/plugin/lua/Scripting.html

### Python Bindings

Third-party Python bindings exist:
- **pyrime**: Python binding of librime for prompt-toolkit integration
- **librime-python**: pybind11 plugin enabling Python scripting within Rime

These are relatively niche. Since the core Rime ecosystem is C/C++, we will use
C++ directly for Rime integration rather than going through Python bindings.

### librime C API

The public C API (`rime_api.h`) allows external programs to control Rime:

- Initialize/finalize the engine
- Create sessions, process key events
- Access candidates, commit text
- Configure schemas and deployment

## Deployment

After creating/modifying schema files, a "deploy" step compiles them into binary
files for fast lookup:

- `<dict>.table.bin` — forward lookup index (code → characters)
- `<dict>.reverse.bin` — reverse lookup index (character → code)
- `<schema_id>.prism.bin` — spelling algebra mappings

Deployment is triggered via the platform client UI (e.g., "Deploy" button).

## Relevance to Predictable Pinyin

For our project, the key architectural considerations are:

1. **Fork Rime's existing clients** — Squirrel (macOS), Weasel (Windows),
   ibus-rime (Linux) already provide OS integration and UI. We fork and modify
   their C++ code to add our custom logic rather than building a separate
   application or custom UI.
2. **Use `script_translator`** for pinyin→character lookup (leveraging existing
   pinyin dictionaries).
3. **Build custom logic in C++** — the multi-phase state machine (pinyin → stroke →
   candidate selection), stroke filtering, candidate reordering, and the custom
   navigation keys (J/K/L/F/D). All implemented as C++ processors/filters within
   the forked Rime codebase.
4. **Use Dart for data processing** — standalone scripts for parsing stroke data,
   building frequency maps, and other offline data preparation tasks. Dart is
   preferred over Python for its type safety and dependency management.
5. **Ship a custom dictionary** that includes stroke data per character alongside
   pinyin data for runtime lookup.

## References

- [Rime Schema Design Guide](https://rimeinn.github.io/rime/schema-design.html)
- [Rime Customization Guide](https://rimeinn.github.io/rime/customization-guide.html)
- [Rime Description (detailed settings reference)](https://github.com/LEOYoon-Tsaw/Rime_collections/blob/master/Rime_description.md)
- [librime-sample (C++ plugin example)](https://github.com/rime/librime-sample)
- [librime C API (rime_api.h)](https://github.com/rime/librime/blob/master/src/rime_api.h)
