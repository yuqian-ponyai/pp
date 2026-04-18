# Experimental Word Input Support Plan

Experimental plan for adding word/phrase input alongside the existing
single-character flow. Not finalized — design details may change.

## Motivation

Chinese input methods are typically used for word-level input (e.g., typing
"zhongguo" to commit 中国). Single-character input with stroke disambiguation
is the core innovation of Predictable Pinyin, but word input is essential for
practical daily use — it reduces keystrokes significantly for common phrases.

## Current Architecture

The state machine processes one character at a time:

```
pinyin → ; → strokes → J/K/L/F → SPACE (commit single character)
```

Key constraints in the current code:

| Component | Behavior |
|-----------|----------|
| `CurrentCandidates()` | Filters out multi-character strings via `IsSingleUtf8Char` |
| `FrequencySorter` | Only has rank data for single characters (hanziDB.csv) |
| `MatchesPinyin()` | Matches a single character's pinyin against the buffer |
| `StrokeFilter` | Only has stroke data for single characters |
| Rime session | Already returns word candidates (e.g., 中国 for "zhongguo") — we discard them |

## Design

Words and characters share the same flow. Each `;` starts stroke matching for
the next character in the candidate:

```
pinyin → SPACE                             (commit top — may be a word)
pinyin → ; → strokes → SPACE              (1-char stroke narrowing)
pinyin → ; → strokes → ; → strokes → SPACE  (2-char stroke narrowing)
pinyin → ; → strokes → J/K/L/F → SPACE    (select then commit)
```

### Per-character stroke matching via multiple `;`

Each `;` press advances stroke matching to the next character position. Stroke
keys typed between two `;` delimiters are matched against the corresponding
character's stroke sequence as a prefix.

Example with 中国 (中 strokes: `szhs`, 国 strokes: `szzshh`):

| Input | Meaning |
|-------|---------|
| `zhongguo;s` | 1st char strokes start with `s` → 中(szhs) ✓ |
| `zhongguo;sz` | 1st char strokes start with `sz` → 中(szhs) ✓ |
| `zhongguo;sz;s` | 1st char `sz` ✓ AND 2nd char starts with `s` → 国(szzshh) ✓ |
| `zhongguo;;sz` | 1st char unconstrained, 2nd char starts with `sz` → 国(szzshh) ✓ |

For single characters, behavior is unchanged — one `;` followed by strokes.

This design is more intuitive than concatenated strokes because:
1. You always know which character's strokes you're typing.
2. You can type as many or as few strokes per character as needed.
3. `;` provides a clear visual boundary.
4. You can skip a character's strokes entirely with `;;`.

### Flow comparison

| Goal | Keystrokes |
|------|-----------|
| Input 中国 (word, quick) | `z h o n g g u o SPACE` |
| Input 中国 (1st char strokes) | `z h o n g g u o ; s z SPACE` |
| Input 中国 (both chars strokes) | `z h o n g g u o ; s z ; s z SPACE` |
| Input 中国 (2nd char only) | `z h o n g g u o ; ; s z SPACE` |
| Input 中 (character, quick) | `z h o n g SPACE` |
| Input 中 (character, with strokes) | `z h o n g ; s z h s SPACE` |

### Preedit display

Show the per-character stroke segments separated by `;` in the preedit:

```
zhong guo | sz;s
```

This makes it clear that `sz` applies to the first character and `s` to the
second.

### Candidate ordering

- **Pinyin phase**: trust Rime's native ordering (prioritizes words and common
  phrases). Do not apply our single-character frequency sort.
- **Stroke/selecting phases**: apply our frequency sort only to single-character
  candidates. Word candidates keep Rime's ordering and appear before single
  characters (more specific matches).

## Implementation

### Data model change

The `stroke_buffer_` changes from `std::string` to
`std::vector<std::string>`:

```cpp
// Before
std::string stroke_buffer_;

// After
std::vector<std::string> stroke_segments_;  // one segment per character position
```

When entering stroke phase (first `;`), push one empty segment. Each
subsequent `;` in stroke phase pushes another empty segment. Stroke keys
append to the last segment.

### State machine changes

In `HandleKey`, when `;` is pressed in stroke phase:

```cpp
if (phase_ == Phase::kStrokeInput && key == ';') {
  stroke_segments_.push_back("");  // start strokes for next character
  return Snapshot();
}
```

### Changes to `StrokeFilter`

Add a per-character filtering method. Each segment in `stroke_segments_` is
matched against the stroke prefix of the corresponding character in the
candidate word:

```cpp
bool StrokeFilter::MatchesPerCharStrokes(
    const std::string& word,
    const std::vector<std::string>& segments) const {
  // Split word into individual UTF-8 characters.
  std::vector<std::string> chars = SplitUtf8(word);

  // Word must have at least as many characters as stroke segments.
  if (chars.size() < segments.size()) return false;

  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].empty()) continue;  // unconstrained position (;;)
    auto it = strokes_.find(chars[i]);
    if (it == strokes_.end()) return false;
    const std::string& char_strokes = it->second;
    if (char_strokes.compare(0, segments[i].size(), segments[i]) != 0)
      return false;
  }
  return true;
}
```

Update `Filter()` to call `MatchesPerCharStrokes` instead of the old
single-prefix approach:

```cpp
std::vector<std::string> StrokeFilter::Filter(
    const std::vector<std::string>& candidates,
    const std::vector<std::string>& stroke_segments) const {
  bool all_empty = std::all_of(stroke_segments.begin(),
                               stroke_segments.end(),
                               [](const std::string& s) { return s.empty(); });
  if (all_empty) return candidates;  // no constraints

  std::vector<std::string> result;
  for (const auto& c : candidates) {
    if (MatchesPerCharStrokes(c, stroke_segments)) {
      result.push_back(c);
    }
  }
  return result;
}
```

### Changes to `NextStrokeAfterPrefix`

Show the next stroke for the character at the current (last) segment position:

```cpp
std::string StrokeFilter::NextStrokeForSegment(
    const std::string& word,
    const std::vector<std::string>& segments) const {
  std::vector<std::string> chars = SplitUtf8(word);
  std::size_t pos = segments.empty() ? 0 : segments.size() - 1;
  if (pos >= chars.size()) return {};
  auto it = strokes_.find(chars[pos]);
  if (it == strokes_.end()) return {};
  const std::string& prefix = segments.empty() ? "" : segments[pos];
  if (prefix.size() >= it->second.size()) return {};
  if (it->second.compare(0, prefix.size(), prefix) != 0) return {};
  return std::string(1, it->second[prefix.size()]);
}
```

### Changes to `CurrentCandidates()`

Remove the `IsSingleUtf8Char` filter entirely. Both words and characters
participate in stroke filtering via per-character segments:

```cpp
std::vector<std::string> PredictableStateMachine::CurrentCandidates() const {
  auto candidates = stroke_filter_.Filter(
      session_->GetCandidates(64), stroke_segments_);

  if (!pinyin_buffer_.empty()) {
    const bool prefix =
        phase_ == Phase::kPinyinInput || !trie_.Contains(pinyin_buffer_);
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [this, prefix](const std::string& c) {
                         return !frequency_sorter_.MatchesPinyin(c, pinyin_buffer_, prefix);
                       }),
        candidates.end());
  }

  if (phase_ != Phase::kPinyinInput) {
    frequency_sorter_.Sort(candidates);
  }

  return candidates;
}
```

### Changes to `BuildPreedit` / preedit display

Show stroke segments joined by `;`:

```cpp
if (phase_ == Phase::kStrokeInput || phase_ == Phase::kSelecting) {
  preedit += " | ";
  for (std::size_t i = 0; i < stroke_segments_.size(); ++i) {
    if (i > 0) preedit += ";";
    preedit += stroke_segments_[i];
  }
}
```

### Changes to `BuildHint()`

Update the stroke-phase hint:

```
"Strokes for char N. h/s/p/d/n/z adds stroke. ; next char. J/K/L/F selects. SPACE commits."
```

### Backspace behavior

- If the current (last) segment is non-empty: pop its last stroke.
- If the current segment is empty and there are multiple segments: pop the
  segment (go back to previous character's strokes).
- If the current segment is empty and it's the only segment: return to pinyin
  phase (same as today).

```cpp
case Phase::kStrokeInput:
  if (!stroke_segments_.back().empty()) {
    stroke_segments_.back().pop_back();
  } else if (stroke_segments_.size() > 1) {
    stroke_segments_.pop_back();
  } else {
    stroke_segments_.clear();
    phase_ = pinyin_buffer_.empty() ? Phase::kIdle : Phase::kPinyinInput;
  }
  break;
```

### Test changes

1. Add `FakeSession` entries that return multi-character candidates.
2. Add stroke data for more characters to `WriteSampleStrokeDict` (e.g., 国).
3. Verify single `;` works as before for single characters.
4. Verify `pinyin ; strokes ; strokes` narrows word candidates correctly.
5. Verify `;;strokes` (skip first character) works.
6. Verify backspace across `;` boundaries.
7. Verify SPACE from stroke phase commits a word.
8. Verify J/K/L/F works to select among word candidates.
9. Verify preedit shows `| sz;s` format.

## Files to modify

| File | Change |
|------|--------|
| `src/predictable_state_machine.h` | `stroke_buffer_` → `stroke_segments_` (vector) |
| `src/predictable_state_machine.cc` | Multiple-`;` handling, updated backspace, preedit, hint |
| `src/stroke_filter.h/.cc` | `MatchesPerCharStrokes`, `NextStrokeForSegment`, updated `Filter` |
| `tests/test_support.h` | Add word-related test data |
| `tests/integration_test.cc` | New word-input test cases |
| `README.md` | Mention word input in Method section |
| `doc/input-flow.md` | Update state machine description |

No new files needed. No new dependencies.

## Effort estimate

| Task | Estimate |
|------|----------|
| `stroke_segments_` data model + state machine `;` / backspace handling | 0.5 day |
| `StrokeFilter::MatchesPerCharStrokes` + updated `Filter` / `NextStrokeForSegment` | 0.5 day |
| Remove `IsSingleUtf8Char` filter; conditional frequency sort | 0.5 day |
| Add tests for word input and multi-`;` stroke filtering | 1 day |
| Update README, input-flow.md, hints, preedit display | 0.5 day |
| Manual testing with real Rime data | 0.5 day |
| **Total** | **~3.5 days** |

## Risks

| Risk | Mitigation |
|------|------------|
| Rime word ordering may not match user expectations | Can add word frequency data later |
| `MatchesPinyin` pass-through for words may let irrelevant candidates through | Rime already filters by pinyin; our filter is a secondary check |
| Characters missing from stroke dict cause words to be dropped | Our stroke dict covers ~9000 common characters; rare-character words would be filtered out |
| Multiple `;` adds complexity to the state machine | Backspace reversal is straightforward; preedit display makes state clear |
| Users may not discover the multi-`;` feature | Hint text explains `;` advances to next character |

## Future extensions

- **Word frequency data**: Import a word frequency list to improve ordering.
- **Smart word/character priority**: Learn from user behavior which candidates
  to prioritize (conflicts with "predictability" goal — needs careful design).
- **Phrase completion**: After committing a word, suggest the next likely word
  based on context (tension with predictability).
