Human review feedback of the docs or code. The 1st level title is the
git hash that the review is for. They are sorted by time in descending order.

# Git hash e8bb63ea (2026-04-20)
- The stroke `t` and `d` are very hard to distinguish. Combine them into `d`.

# Git hash 140f2a8 (2026-04-07)
- In stroke phase, instead of just showing the next stroke, show the full stroke sequence
  remaining so it's easier for users to know what **sequence** to type next.
- Allow TAB key to autocomplete all strokes that are shared by the top 2 candidates. This
  is very useful for candidates that share a lot of strokes. For example, `努` and `怒`.

# Git hash 0db730d2 (2026-04-06)
- Some 多音字 can't be typed using multiple ways of pinyin. For example, `的` can be
  typed as `de`, but not`di`; `沈` can be typed as `chen`, but not `shen`. Fix it
  and add tests.

# Git hash 119e35b9 (2026-03-26) user feedback
- When `ctrl` or `cmd` key is pressed, do not capture keys in the input method. For example,
  `ctrl+c`, `cmd+c`, `ctrl+v`, `cmd+v` are copy/paste commands we shouldn't capture.
- In ibus, support `shift` to swtich between Chinese and English input (including punctuation).
  The f...x already has that.

# Git hash eb78349f (2026-03-24)
- Support `'` to split the word. For example, `ni'ao`. Wtihout `'`, always match the longest single
  char. For example, `niao` should only show `鸟` without `尼奥`. Only `ni'ao` should show `尼奥`.
  Meanwhile, don't break word that don't require `'`. For example, `zhongguo` should show `中国`.
  Add tests.
- In stroke phase, if a char fully matches the stroke, put it higher than other char without
  full match, even if the other char has higher frequency. For example, `shi;hs` should put `十`
  first, not `事`. Fix it and add tests.
- When only part of the pinyin is committed, the state machine should go back to the stroke phase
  with the remaining input. For example, `qianshan;jj ` would commit `千`, and then go back to the
  stroke phase with `shan`. Add tests.

# Git hash b4029a2 (2026-03-24)
- `yuqian` still has some issues. While `yuqian;ddz` shows `宇`, `yuqian;ddz;ddz`
  doesn't show `宇骞`. Fix it and add tests.
- Support all punctuation marks such as `!` and others. The only unique case is `;`,
  which should not commit and output ';' during active input. However, when there's
  no input, `;` should still write a Chinese semicolon.

# Git hash 1046aad (2026-03-24)
- The stroke selection for `yuqian` is broken. It can't use stroke or any way to
  get `宇骞`. For example, `yuqian;ddz` doesn't show `宇` at all. Fix add tests.
- I expect `sui` and `sui;` to have the same candidate list. However, the former
  puts `岁` first, and the latter puts `随` first. Fix it and add tests.
- When inputing `,` or `.`, it should return Chinese punctuation marks instead of
  English ones. Fix it and add tests.

# Git hash 3661cd94 (2026-03-23)
- Let's use `;` to enter stroke phase, and always use SPACE to commit (either the
  top candidate, or the currently selected candidate). This is closer to user
  habits from old input methods. Update docs, tests, and UI hints.
- When one type `c`, and then type the commit key (previously `;`, SPACE in the
  future), the character is not committed. Fix it, and add tests.
- The word input (in addition to the character input) seems really important due
  to the user habits. Write a more detailed plan on how to support it, and
  estimate its effort.

# Git hash d16cb5b (2026-03-23)
- Make `F` advance 10 candidates since a page now has 10 candidates.
- The `J, K, L` hint seems wrong by 1. For example, when `J` should jump to 2,
  it shows 1. Fix it. Ensure `F` doesn't have the same problem. Add tests.
- `;` doesn't commit the top candidate at all either in stroke or selection
  phase. Fix it and add tests.

# Git hash aa355f1 (2026-03-23)
- Before the first SPACE, the candidate list is still wrong: `niao` would show `你`.
  After the SPACE, `你` disappears. Make it disappear before the SPACE as well.
- Add `d` for 捺/点 according to the latest README. Now `d` is no longer a key to
  enter the selection phase. Add tests.
- When `;` is pressed, directly commit the top candidate to save 2 SPACE keys.
  Show that in the hint too. Update docs.

# Git hash aa355f1 (2026-03-23)
- When `scripts/install-f...x.sh` is run after a code change, often `f...x -rd` is
  needed to let the change take effect. Update echo message to explain and remind.
- Before the first SPACE, now the candidate list is empty. Fix it as we still want
  to show the candidates. We just don't want to show `h, s, p, n, z` in the hint.
  You can show `␣` as a hint instead.
- When `niao` is typed, I still see `你` in the candidate list. It should be filtered
  out. Only `鸟` with a full match to `niao` should be shown. Fix it and correct tests.

# Git hash f65db7bc (2026-03-23)
- Do not show `h, s, p, n, z` in hint before the stroke phase as it's misleading.
  To make it more predictable, do not enter the stroke phase until the user
  types a space, even if the current pinyin input has no valid extension.
- Since `J, K, L, F, D` is disjoint with `h, s, p, n, z`, let user to skip SPACE
  and directly typing `J, K, L, F, D` to select candidates during the stroke phase.
  It's Ok to enter the selection phase and not accept stroke keys anymore. To make
  it more predictable, let's make `J,K, L, F, D` the only valid keys to enter the
  selection phase. No more SPACE is needed to enter it. A space would instead
  directly commit the current candidate in the stroke phase. Update code, docs,
  and tests accordingly.
- When `niao` is typed, I don't expect `你` or `尼奥` to be shown as candidates.
  I only want to see `鸟` and similar chars as candidates. Fix it and add tests.

# Git hash 66a5b09 (2026-03-23)
- Add unit tests to prevent regressions for the fixes in 724a891.
- The key hint in the candidate is wrong. For example, it always has `h, s, p,
  n` in order, but the correct hint should depend on the stroke of the candidate.
- When selecting using `J, K, L, F, D`, the hint should be `J, K, L, F, D`
  instead of `h, s, p, n, z`. Also make sure that the hint is updated when the key
  is pressed (e.g., `J` hint should move when `J` is pressed).

# Git hash 724a891 (2026-03-23)
- Change Chinese name to 霹雳拼音. Update all code and docs.
- When I press number keys to select candidates, F...X seems to crash. Fix it.
- Wen predictable pinyin is enabled, the backspace no longer removes the last
  character in the text editor. Fix it.
- In the candidate list, replace the number 0-9 with the real keys to select them
  such as `h, s, p, n, z` and `J, K, L, F, D`.


# Git hash 6b21a0ba (2026-03-20)
- In integration_test.cc, double check if `a` should auto end. Could there be `ao` or `ai`?
- Update timeline in plan.md.

# Git hash 9a80136 (2026-03-20)
- Update README.md: build.sh is not just for Phase 4, it's for all phases.
- Update plan.md: mark phase 3 as done as we are now in phase 4.
- Fix the \n bugs in mermaid diagrams.

# Git hash ba7c5fb (2026-03-20)
- Update README.md on using (#CPU - 1) concurrency to build the project. Ensure all future agents to
  follow this setup.

# Git hash c47b0cc (2026-03-20)
- Introduce a unit test framework so each unit test can show up its title and status. The current
  output `1/1 Test #1: pp_core_tests ....................   Passed    0.00 sec` is not very helpful.
- Remove the reinvented wheel of test support code by using a popular test framework.
- Document how to config clangd so IDE can jump to the definition of the code.

# Git hash dd3480c (2026-03-20)
- In README.md, clarify `cmake -B` is only needed once after the modification of CMakeLists.txt.
- Try your best to simplify the code to minimize complexity, reduce repeated code or code that other
  libs can already provide.
- Write unit tests and document how to run them.
- Write a README.md doc in the C++ source file folder. Ideally, there's also a nice mermaid diagram
  to illustrate the code structure and how they interact with each other.

# Git hash 5513c78 (2026-03-20)

- Update root README.md on how to input strokes.
- Update root README.md on how to use cmake to compile the project.

# Git hash 5de142b (2026-03-19)

- Remove build_pinyin_syllables.dart. It is not needed as it's as verbose as pinyin_syllables.txt.
- Remove frequency_map.tsv and its script because hanzi_db.csv is already sorted by frequency.
- Remove frequency_map.tsv and future code can directly read stroke.dict.yaml.
- Update the plan.md to reflect the simplifications above.
- In the future, do not leave the test folder empty. Write test in scripts/test to either test the
  scripts in bin, or test the generated data. For now, I think we don't need any scripts.

# Git hash 9776046 (2026-03-19)

- Manually improved the state machine diagram in input-flow.md. No more change is needed.
- Updated name from `Consistent Pinyin` to `Predictable Pinyin`.
- We should be good to go for the Phase 1 implementation.

# Git hash 8c526ce7 (2026-03-19)

- State machine diagram in input-flow.md has strange edges. Try to make it look better.
- Prefer using Dart instead Python for standalone data processing tasks due to its better type
  checks and dependency management.
- Only use Python if rime has some nice Python bindings. If there's only C/C++, directly use
  C/C++.
- Do not use Flutter for UI. Use the existing tech stack of Rime. We can fork Rime and change its
  code if needed.

# Git hash 72004455 (2026-03-19)

- Replace `D, F, J, K` with Rime's 5 stroke keys `h, s, p, n, z`.
- Do not use Lua. You can use C++, Python, or Dart as programming languages.
- State machine diagram in input-flow.md is broken. Use mermaid instead of ASCII art.
