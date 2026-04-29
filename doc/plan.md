# Implementation Plan

Overall plan for building the Predictable Pinyin input method.

## Architecture Decision

**Fork Rime's existing clients** and add custom C++ logic directly.

Rationale:
- Rime already handles cross-platform deployment (Linux, macOS, Windows) with
  existing clients: Squirrel (macOS), Weasel (Windows), ibus-rime (Linux)
- Rime provides pinyin dictionary, stroke dictionary, and deployment infrastructure
- Custom logic (state machine, stroke filtering, candidate reordering) will be
  implemented in **C++** as custom processors/filters within the forked Rime codebase
- **No custom UI** — use Rime's existing candidate window and preedit display
- **Dart** for any offline data processing or test scripts — preferred over Python
  for type safety and dependency management
- Python bindings exist (pyrime, librime-python) but are niche; since Rime's core
  is C/C++, we use C++ directly

See [rime-framework.md](./rime-framework.md) for details on the Rime architecture.

## Deliverables

The project will produce a Rime-based input method with these components:

**Rime schema and data files:**

| File | Description |
|------|-------------|
| `rime/predictable_pinyin.schema.yaml` | Schema definition (engine config, key bindings) |
| (future) `predictable_pinyin.dict.yaml` | Custom dictionary with pinyin + hanziDB frequency weights |

**Custom C++ components (in forked Rime clients):**

| Component | Description |
|-----------|-------------|
| State machine processor | Multi-phase input processor (pinyin → stroke → selection) |
| Stroke filter | Filter candidates by stroke sequence (h/s/p/n/z) |
| Pinyin metadata loader | Validate readings and support virtual candidates |
| Pinyin trie | Valid pinyin syllable trie for auto-end detection |

**Raw data files (read directly at runtime, no preprocessing):**

| File | Source | Description |
|------|--------|-------------|
| `data/raw/hanzi_db.csv` | [hanziDB.csv](https://github.com/ruddfawcett/hanziDB.csv) | Character frequency database (sorted by rank) |
| `data/raw/stroke.dict.yaml` | [rime-stroke](https://github.com/rime/rime-stroke) | Character → stroke sequence (h/s/p/n/z) |


## Phases

### Phase 1: Data Preparation ✅

**Goal**: Assemble the raw data files needed at runtime.

Tasks:
1. ✅ Snapshot `hanzi_db.csv` from [ruddfawcett/hanziDB.csv](https://github.com/ruddfawcett/hanziDB.csv)
2. ✅ Snapshot `stroke.dict.yaml` from [rime/rime-stroke](https://github.com/rime/rime-stroke)

Both files are stored in `data/raw/` and read directly — no preprocessing needed.
`hanzi_db.csv` is already sorted by frequency rank, and `stroke.dict.yaml` is the
native Rime format that the C++ code can parse directly.

### Phase 2: Basic Rime Schema ✅

**Goal**: Get a working pinyin input method with standard behavior.

Tasks:
1. ✅ Create `rime/predictable_pinyin.schema.yaml` with basic engine components
   (uses `pinyin_simp` dictionary for standard pinyin behavior)
2. Custom `predictable_pinyin.dict.yaml` with hanziDB weights deferred to Phase 5
   (the existing `pinyin_simp` dict already provides good pinyin→character mapping)
3. ✅ Install ibus-rime and librime-dev on dev machine
4. ✅ Deploy schema to ibus-rime (`~/.config/ibus/rime/`) and build
5. ✅ Verify C++ build environment: cmake + librime-dev linking works

### ✅ Phase 3: State Machine (C++ Processor)

**Goal**: Implement the multi-phase input flow.

This is the core innovation. Implement a custom C++ processor that intercepts
keystrokes and manages the state transitions described in [input-flow.md](./input-flow.md).

Tasks:
1. ✅ Implement PINYIN_INPUT state — delegate to Rime's speller, detect auto-end
2. ✅ Implement STROKE_INPUT state — capture h/s/p/n/z keys, maintain stroke buffer
3. ✅ Implement SELECTING state — handle J/K/L/F/D navigation
4. ✅ Implement backspace behavior across all states
5. ✅ Implement auto-transition rules (pinyin auto-end, single-candidate auto-commit)
6. ✅ Show state hints outside editable preedit where the framework supports it

Estimated effort: 3-5 days

### ✅ Phase 4: Stroke Filtering (C++ Filter)

**Goal**: Filter candidates based on stroke input.

Tasks:
1. ✅ Parse `stroke.dict.yaml` into memory at init
2. ✅ Implement stroke matching: given accumulated strokes (e.g., "shpn"), filter
   candidates to those whose stroke sequence starts with that prefix
3. ✅ Handle partial matches (user may type fewer strokes than the character has)
4. ✅ All 5 stroke types (h/s/p/n/z) are supported

Estimated effort: 2-3 days

### ✅ Phase 5: Candidate Metadata (C++ Filter)

**Goal**: Preserve Rime order while loading metadata needed for filtering.

Tasks:
1. ✅ Parse `hanzi_db.csv` into memory at init (already sorted by frequency rank)
2. ✅ Load supplementary Rime pinyin readings for 多音字
3. ✅ Validate single-character candidates against all known readings
4. ✅ Preserve Rime candidate order after filtering

Estimated effort: 1 day

### ✅ Phase 6: Selection Navigation

**Goal**: Implement the custom J/K/L/F/D candidate navigation.

Tasks:
1. ✅ Override default candidate selection behavior in SELECTING state
2. ✅ J = next (skip 1), K = skip 2, L = skip 4, F = next page (8), D = undo
3. ✅ Show candidate list with position indicators
4. ✅ Space = commit selected candidate

Estimated effort: 1-2 days

### ✅ Phase 7: Polish and Testing

**Goal**: End-to-end testing, edge cases, UX refinement.

Tasks:
1. ✅ Test full end-to-end flow (pinyin → stroke → select → commit)
2. ✅ Test multi-reading characters (多音字: 重 zhòng/chóng)
3. ✅ Test edge cases: backspace at state boundaries, empty stroke auto-commit/select,
   auto-end of extensionless syllables, idle backspace no-op
4. ✅ Verify deterministic ordering (same input always produces identical candidates)
5. ✅ Performance testing (data loading under 1 second sanity check)
6. ✅ Update user documentation

Estimated effort: 2-3 days

### Phase 8: Cross-Platform Deployment

**Goal**: Port the custom C++ components to all three platform clients.

Tasks:
1. ✅ Linux (ibus): standalone engine executable — see [phase-8-ibus-linux-plan.md](./phase-8-ibus-linux-plan.md)
2. ✅ macOS (IMK `.app` bundle) — see [phase-8-macos-plan.md](./phase-8-macos-plan.md)
3. Weasel (Windows) with custom components
4. Create build instructions and CI for each platform
5. Provide pre-built binaries or installation packages

Estimated effort: 2-3 days per platform

Detailed planning:
- ibus: [phase-8-ibus-linux-plan.md](./phase-8-ibus-linux-plan.md)
- macOS: [phase-8-macos-plan.md](./phase-8-macos-plan.md)

## Open Design Questions

These need resolution before or during implementation:

### 1. Which Rime clients to fork first?

We will fork and modify Rime's existing platform clients. Priority order:

- **Linux first** (ibus-rime) — dev machine runs ibus with ibus-rime
- **macOS** (Squirrel) — Swift/Obj-C frontend, librime C++ core
- **Windows** (Weasel) — C++ throughout

### 2. Single-character vs. word input?

The README describes the flow for "each Chinese character" individually. Should we
support word/phrase input? Options:
- **Character-only**: simpler, matches the README spec exactly
- **Word support**: more practical for daily use, but the stroke disambiguation
  flow would need to work differently for multi-character words

Recommendation: start with character-only (Phase 1-7), add word support later.

### 3. Tone input?

The README does not mention tone input. Standard pinyin has 4 tones + neutral.
Not including tones means more homophones to disambiguate with strokes. This
seems intentional — tones are hard to type and the stroke disambiguation compensates.

### 4. How to handle the pinyin "space" delimiter when auto-end fires?

If the pinyin auto-ends (no valid continuation), the user goes straight to
STROKE_INPUT without pressing space. But what if the user intended a different,
shorter pinyin? For example: typing "zha" — auto-end might not fire (since "zhai",
"zhan", etc. are valid), but the user wanted "zha" not "zhan". The space is needed
in this case. Auto-end only helps when NO continuation is valid.

### 5. Learning curve considerations

The h/s/p/n/z stroke keys overlap with pinyin letters, and J/K/L/F/D selection keys
have different meanings in different states. This could be confusing initially.
Consider adding a "training mode" that shows extra hints.

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Rime client fork maintenance burden | Medium | Minimize diff; isolate custom code into separate files |
| Cross-platform C++ compilation | Medium | Use CMake; test on CI early; fork one platform first |
| Rime client codebase complexity | Medium | Study existing processor/filter implementations as templates |
| User confusion with multi-state input | Medium | Clear hints, training mode |
| hanziDB coverage gaps | Low | 100% coverage confirmed for rime-stroke overlap |

## Timeline

| Phase | Status | Actual |
|-------|--------|--------|
| Phase 1: Data Preparation | ✅ done | 2026-03-19 |
| Phase 2: Basic Schema | ✅ done | 2026-03-19 |
| Phase 3: State Machine | ✅ done | 2026-03-20 |
| Phase 4: Stroke Filtering | ✅ done | 2026-03-20 |
| Phase 5: Candidate Sorting | ✅ done | 2026-03-20 |
| Phase 6: Selection Nav | ✅ done | 2026-03-20 |
| Phase 7: Polish & Testing | ✅ done | 2026-03-20 |
| Phase 8: Cross-Platform | Linux ibus + macOS done; Windows pending | started 2026-03-20 |
| Word Input | ✅ done | 2026-03-23 |

Phases 1-7 completed in 2 days. Phase 8 is partially complete: Linux ibus and
macOS are implemented, while Windows remains. Word input with per-character
stroke narrowing via multiple `;` is implemented.

## References

- [rime-framework.md](./rime-framework.md) — Rime architecture notes
- [stroke-data.md](./stroke-data.md) — stroke data notes
- [character-database.md](./character-database.md) — hanziDB notes
- [input-flow.md](./input-flow.md) — input state machine design
- [Project README](../README.md) — original specification
