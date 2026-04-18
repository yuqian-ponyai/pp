#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "frequency_sorter.h"
#include "pinyin_trie.h"
#include "predictable_state_machine.h"
#include "stroke_filter.h"
#include "test_support.h"

namespace predictable_pinyin::test {

namespace {

struct TestFixture {
  std::filesystem::path prism_path = WriteSamplePrism();
  std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(prism_path.parent_path());
  std::filesystem::path hanzi_db_path = WriteSampleHanziDb(prism_path.parent_path());
  std::filesystem::path pinyin_dict_path = WriteSamplePinyinDict(prism_path.parent_path());
  ScopedDirectoryCleanup cleanup{prism_path.parent_path()};
};

}  // namespace

// --- Edge cases ---

TEST_CASE("Single candidate auto-commits when stroke SPACE is pressed", "[integration]") {
  TestFixture f;
  FakeSession session({{"a", {"啊"}}});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('a');
  machine.HandleKey(';');  // → kStrokeInput
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "啊");
}

TEST_CASE("SPACE in stroke commits top candidate directly", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → kStrokeInput
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "你");
}

TEST_CASE("SPACE from pinyin commits top candidate directly", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "你");
}

TEST_CASE("Single letter c + SPACE commits top candidate", "[integration]") {
  TestFixture f;
  FakeSession session({{"c", {"才", "从"}}});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('c');
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "才");
}

TEST_CASE("Incomplete pinyin via semicolon then SPACE still commits", "[integration]") {
  TestFixture f;
  FakeSession session({{"c", {"才", "从"}}});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('c');
  machine.HandleKey(';');  // → kStrokeInput (pinyin "c" is incomplete)
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "才");
}

TEST_CASE("Backspace from idle is a no-op", "[integration]") {
  TestFixture f;
  FakeSession session({});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  const auto snap = machine.HandleKey('\b');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.pinyin_buffer.empty());
}

TEST_CASE("Backspace from pinyin to idle when buffer empties", "[integration]") {
  TestFixture f;
  FakeSession session({{"n", {"你"}}});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  const auto snap = machine.HandleKey('\b');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.pinyin_buffer.empty());
}

TEST_CASE("Backspace from selecting to stroke when history is empty", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → kStrokeInput
  machine.HandleKey('J');  // → kSelecting (index 1, history=[0])
  machine.HandleKey('\b');  // undo to index 0, history=[]
  const auto snap = machine.HandleKey('\b');  // empty history → kStrokeInput
  CHECK(snap.phase == Phase::kStrokeInput);
}

// --- Multi-reading characters ---

TEST_CASE("Multi-reading character appears for each pronunciation", "[integration]") {
  TestFixture f;

  FakeSession session({
      {"z", {"在"}}, {"zh", {"中"}}, {"zho", {"中"}},
      {"zhon", {"中"}}, {"zhong", {"中", "重", "种"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
  machine.HandleKey(';');  // → kStrokeInput
  const auto zhong_snap = machine.Snapshot();
  CHECK(zhong_snap.phase == Phase::kStrokeInput);
  bool found_zhong = false;
  for (const auto& c : zhong_snap.candidates) {
    if (c == "重") found_zhong = true;
  }
  CHECK(found_zhong);
}

// --- Deterministic ordering ---

TEST_CASE("Repeated identical input produces identical candidate order", "[integration]") {
  TestFixture f;
  std::vector<std::string> runs[3];
  for (int run = 0; run < 3; ++run) {
    FakeSession session({
        {"z", {"在"}}, {"zh", {"中"}}, {"zho", {"中"}},
        {"zhon", {"中"}}, {"zhong", {"种", "重", "中"}},
    });
    PredictableStateMachine machine(&session);
    REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));
    for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
    machine.HandleKey(';');  // → kStrokeInput
    runs[run] = machine.Snapshot().candidates;
  }
  REQUIRE(runs[0] == runs[1]);
  REQUIRE(runs[1] == runs[2]);
}

// --- Performance ---

TEST_CASE("Data loading completes within reasonable time", "[integration]") {
  TestFixture f;
  const auto start = std::chrono::steady_clock::now();

  PinyinTrie trie;
  REQUIRE(trie.LoadFromPrismFile(f.prism_path));

  StrokeFilter stroke_filter;
  REQUIRE(stroke_filter.LoadFromStrokeDict(f.stroke_dict_path));

  FrequencySorter sorter;
  REQUIRE(sorter.LoadFromCsv(f.hanzi_db_path));

  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  CHECK(ms < 1000);
}

// --- Full flow end-to-end ---

TEST_CASE("Full flow: pinyin, stroke, select via J, commit", "[integration]") {
  TestFixture f;

  FakeSession session({
      {"z", {"在", "中"}}, {"zh", {"中"}}, {"zho", {"中"}},
      {"zhon", {"中"}}, {"zhong", {"种", "重", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);

  snap = machine.HandleKey(';');
  CHECK(snap.phase == Phase::kStrokeInput);
  REQUIRE(snap.candidates.size() == 3);
  CHECK(snap.candidates[0] == "中");
  CHECK(snap.candidates[1] == "重");
  CHECK(snap.candidates[2] == "种");

  snap = machine.HandleKey('p');
  REQUIRE(snap.candidates.size() == 2);
  CHECK(snap.candidates[0] == "重");
  CHECK(snap.candidates[1] == "种");

  machine.HandleKey('h');
  machine.HandleKey('s');
  snap = machine.HandleKey('z');
  REQUIRE(snap.candidates.size() == 1);
  CHECK(snap.candidates[0] == "重");

  snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "重");
}

// --- Regression tests for 724a891 fixes ---

TEST_CASE("Space and backspace from idle leave state unchanged with no commit", "[integration]") {
  TestFixture f;
  FakeSession session({});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  SECTION("backspace from idle") {
    const auto snap = machine.HandleKey('\b');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text.empty());
  }

  SECTION("space from idle") {
    const auto snap = machine.HandleKey(' ');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text.empty());
  }
}

// --- Candidate label tests ---

TEST_CASE("Candidate labels show full remaining strokes in stroke input phase", "[integration]") {
  TestFixture f;
  // 中=szhs, 种=phspnszhs, 重=phszhhhsh
  FakeSession session({
      {"z", {"在", "中"}}, {"zh", {"中"}}, {"zho", {"中"}},
      {"zhon", {"中"}}, {"zhong", {"种", "重", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
  machine.HandleKey(';');  // → kStrokeInput
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kStrokeInput);
  REQUIRE(snap.candidates.size() == 3);
  REQUIRE(snap.candidate_labels.size() == 3);
  CHECK(snap.candidate_labels[0] == "szhs");       // 中: full remaining
  CHECK(snap.candidate_labels[1] == "phszhhhsh");  // 重: full remaining
  CHECK(snap.candidate_labels[2] == "phspnszhs");  // 种: full remaining

  snap = machine.HandleKey('p');
  REQUIRE(snap.candidates.size() == 2);
  REQUIRE(snap.candidate_labels.size() == 2);
  CHECK(snap.candidate_labels[0] == "hszhhhsh");  // 重: remaining after "p"
  CHECK(snap.candidate_labels[1] == "hspnszhs");  // 种: remaining after "p"
}

TEST_CASE("Pinyin phase shows candidates with semicolon labels", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"z", {"在", "中"}}, {"zh", {"中"}}, {"zho", {"中"}},
      {"zhon", {"中"}}, {"zhong", {"种", "重", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
  const auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);
  CHECK_FALSE(snap.candidates.empty());
  for (const auto& label : snap.candidate_labels) {
    CHECK(label == ";");
  }
}

TEST_CASE("Candidate labels show J/K/L/F in selecting phase", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟", "逆", "泥"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → kStrokeInput
  machine.HandleKey('J');  // → kSelecting (index=1)
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kSelecting);
  CHECK(snap.selected_index == 1);
  REQUIRE(snap.candidate_labels.size() == snap.candidates.size());

  CHECK(snap.candidate_labels[1].empty());
  CHECK(snap.candidate_labels[2] == "J");
  CHECK(snap.candidate_labels[3] == "K");

  snap = machine.HandleKey('J');
  CHECK(snap.selected_index == 2);
  CHECK(snap.candidate_labels[2].empty());
  CHECK(snap.candidate_labels[3] == "J");
  CHECK(snap.candidate_labels[4] == "K");
}

// --- Pinyin filtering ---

TEST_CASE("Pinyin prefix filter during typing and exact filter after semicolon", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你", "鸟"}}, {"ni", {"你", "尼", "鸟"}},
      {"nia", {"鸟", "你"}}, {"niao", {"鸟", "尿", "你"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);
  bool found_ni = false;
  bool found_niao = false;
  for (const auto& c : snap.candidates) {
    if (c == "你") found_ni = true;
    if (c == "鸟") found_niao = true;
  }
  CHECK(found_ni);
  CHECK(found_niao);

  machine.HandleKey('a');
  machine.HandleKey('o');
  snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);
  for (const auto& c : snap.candidates) {
    INFO("pinyin-phase candidate: " << c);
    CHECK(c != "你");
  }

  // After ; exact match: 鸟(niao) ✓, 尿(niao) ✓, 你(ni) ✗
  snap = machine.HandleKey(';');
  CHECK(snap.phase == Phase::kStrokeInput);
  REQUIRE(snap.candidates.size() == 2);
  CHECK(snap.candidates[0] == "鸟");
  CHECK(snap.candidates[1] == "尿");
}

// --- Semicolon behavior ---

TEST_CASE("Semicolon from pinyin enters stroke phase without committing", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  const auto snap = machine.HandleKey(';');
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.commit_text.empty());
  CHECK_FALSE(snap.candidates.empty());
}

TEST_CASE("Semicolon in stroke phase advances to next character segment", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → kStrokeInput, 1 segment
  auto snap = machine.Snapshot();
  CHECK(snap.stroke_buffer.empty());

  snap = machine.HandleKey(';');  // push 2nd segment
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.stroke_buffer == ";");
  CHECK(snap.commit_text.empty());
}

TEST_CASE("Semicolon is ignored in selecting phase", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  machine.HandleKey('J');  // → kSelecting
  const auto snap = machine.HandleKey(';');
  CHECK(snap.phase == Phase::kSelecting);
  CHECK(snap.commit_text.empty());
}

// --- Stroke keys d and t ---

TEST_CASE("Stroke keys d and t are independent stroke classes",
          "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  REQUIRE(machine.Snapshot().candidates.size() == 2);

  const auto snap_d = machine.HandleKey('d');
  CHECK(snap_d.phase == Phase::kStrokeInput);
  CHECK(snap_d.stroke_buffer == "d");

  const auto snap_t = machine.HandleKey('t');
  CHECK(snap_t.phase == Phase::kStrokeInput);
  CHECK(snap_t.stroke_buffer == "dt");
}

TEST_CASE("Selecting hint uses 1-indexed positions and F advances 10", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟", "逆", "泥"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  machine.HandleKey('J');  // → kSelecting, index=1

  const auto snap = machine.Snapshot();
  CHECK(snap.selected_index == 1);
  CHECK(snap.hint.find("[2]") != std::string::npos);
  CHECK(snap.hint.find(std::string("J\xe2\x86\x92") + "3") != std::string::npos);
  CHECK(snap.hint.find(std::string("K\xe2\x86\x92") + "4") != std::string::npos);
}

TEST_CASE("SPACE commits selected candidate in selecting phase", "[integration]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  machine.HandleKey('K');  // → kSelecting, index=2 (拟 after sort)
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "拟");
}

TEST_CASE("StripPinyinTones produces correct toneless pinyin", "[integration]") {
  CHECK(FrequencySorter::StripPinyinTones("nǐ") == "ni");
  CHECK(FrequencySorter::StripPinyinTones("niǎo") == "niao");
  CHECK(FrequencySorter::StripPinyinTones("zhōng") == "zhong");
  CHECK(FrequencySorter::StripPinyinTones("zài") == "zai");
  CHECK(FrequencySorter::StripPinyinTones("abc") == "abc");
}

// ==========================================================================
// Word input tests
// ==========================================================================

TEST_CASE("Word candidate appears when IsSingleUtf8Char filter is removed", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中", "种"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);
  bool found_word = false;
  for (const auto& c : snap.candidates) {
    if (c == "中国") found_word = true;
  }
  CHECK(found_word);
}

TEST_CASE("SPACE from pinyin commits word candidate", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中", "种"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "中国");
}

TEST_CASE("Word stroke filtering with one segment narrows by first char strokes", "[word_input]") {
  TestFixture f;
  // 中国: 中=szhs 国=szzshh
  FakeSession session({
      {"zhongguo", {"中国", "种国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');  // → kStrokeInput, segment 1
  auto snap = machine.HandleKey('s');  // first char strokes "s" → 中(szhs) matches
  bool found = false;
  for (const auto& c : snap.candidates) {
    if (c == "中国") found = true;
  }
  CHECK(found);

  // "p" prefix doesn't match 中(szhs) but would match 种(phspnszhs)
  machine.HandleKey('\b');  // remove "s"
  snap = machine.HandleKey('p');
  for (const auto& c : snap.candidates) {
    CHECK(c != "中国");
  }
}

TEST_CASE("Word stroke filtering with two segments narrows both chars", "[word_input]") {
  TestFixture f;
  // 中国: 中=szhs 国=szzshh
  FakeSession session({
      {"zhongguo", {"中国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');  // segment 1
  machine.HandleKey('s');  // first char: "s"
  machine.HandleKey('z');  // first char: "sz"
  machine.HandleKey(';');  // segment 2
  auto snap = machine.HandleKey('s');  // second char: "s" → 国(szzshh) matches
  bool found = false;
  for (const auto& c : snap.candidates) {
    if (c == "中国") found = true;
  }
  CHECK(found);
  CHECK(snap.stroke_buffer == "sz;s");
}

TEST_CASE("Word stroke filtering skips first char via empty segment", "[word_input]") {
  TestFixture f;
  // 中国: 中=szhs 国=szzshh
  FakeSession session({
      {"zhongguo", {"中国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');  // segment 1 (empty)
  machine.HandleKey(';');  // segment 2
  auto snap = machine.HandleKey('s');  // second char: "s"
  CHECK(snap.stroke_buffer == ";s");
  bool found = false;
  for (const auto& c : snap.candidates) {
    if (c == "中国") found = true;
  }
  CHECK(found);
}

TEST_CASE("Backspace across segment boundaries in word strokes", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');  // segment 1
  machine.HandleKey('s');
  machine.HandleKey(';');  // segment 2
  machine.HandleKey('s');
  auto snap = machine.Snapshot();
  CHECK(snap.stroke_buffer == "s;s");

  snap = machine.HandleKey('\b');  // pop last stroke from segment 2
  CHECK(snap.stroke_buffer == "s;");

  snap = machine.HandleKey('\b');  // pop empty segment 2 → back to segment 1
  CHECK(snap.stroke_buffer == "s");

  snap = machine.HandleKey('\b');  // pop last stroke from segment 1
  CHECK(snap.stroke_buffer.empty());

  snap = machine.HandleKey('\b');  // empty segment 1 → back to pinyin
  CHECK(snap.phase == Phase::kPinyinInput);
}

TEST_CASE("Word candidates ordered before single chars in stroke phase", "[word_input]") {
  TestFixture f;
  // "zhongguo" is NOT a single syllable, so words appear.
  // After partition: words=[中国], singles sorted by freq=[中(54), 种(736)]
  FakeSession session({
      {"zhongguo", {"中国", "种", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');
  auto snap = machine.Snapshot();
  REQUIRE(snap.candidates.size() == 3);
  CHECK(snap.candidates[0] == "中国");
  CHECK(snap.candidates[1] == "中");
  CHECK(snap.candidates[2] == "种");
}

TEST_CASE("Preedit shows segment separators in stroke phase", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');
  machine.HandleKey('s');
  machine.HandleKey('z');
  machine.HandleKey(';');
  machine.HandleKey('s');
  auto snap = machine.Snapshot();
  CHECK(snap.stroke_buffer == "sz;s");
}

TEST_CASE("Stroke hint shows current character position", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');
  auto snap = machine.Snapshot();
  CHECK(snap.hint.find("Char 1") != std::string::npos);

  machine.HandleKey(';');  // advance to char 2
  snap = machine.Snapshot();
  CHECK(snap.hint.find("Char 2") != std::string::npos);
}

TEST_CASE("SPACE from stroke commits word after multi-segment narrowing", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');
  machine.HandleKey('s');  // first char stroke
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "中国");
}

TEST_CASE("J/K/L/F selects among word candidates", "[word_input]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "重国", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');  // → kStrokeInput
  machine.HandleKey('J');  // → kSelecting
  const auto snap = machine.HandleKey(' ');
  CHECK(snap.phase == Phase::kIdle);
  // After ordering: words first [中国, 重国], then singles [中]. J→index 1 → 重国
  CHECK(snap.commit_text == "重国");
}

TEST_CASE("Candidate labels show remaining strokes for word at current segment", "[word_input]") {
  TestFixture f;
  // 中国: 中=szhs 国=szzshh
  FakeSession session({
      {"zhongguo", {"中国"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  machine.HandleKey(';');  // segment 1
  auto snap = machine.Snapshot();
  REQUIRE(snap.candidates.size() == 1);
  CHECK(snap.candidate_labels[0] == "szhs");  // full remaining strokes for 中

  machine.HandleKey(';');  // segment 2
  snap = machine.Snapshot();
  REQUIRE(snap.candidates.size() == 1);
  CHECK(snap.candidate_labels[0] == "szzshh");  // full remaining strokes for 国
}

// ==========================================================================
// Multi-syllable pinyin (issue: yuqian;ddz should show 宇)
// ==========================================================================

TEST_CASE("Multi-syllable pinyin preserves single-char candidates from Rime", "[multi_syllable]") {
  TestFixture f;
  FakeSession session({
      {"yuqian", {"宇骞", "宇", "骞"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("yuqian")) machine.HandleKey(c);
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);
  bool found_yu = false;
  for (const auto& c : snap.candidates) {
    if (c == "宇") found_yu = true;
  }
  CHECK(found_yu);
}

TEST_CASE("Multi-syllable pinyin with stroke filtering narrows correctly", "[multi_syllable]") {
  TestFixture f;
  // 宇=nnzhs, 骞=nnzhhsshpnzzh
  FakeSession session({
      {"yuqian", {"宇骞", "宇", "骞"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("yuqian")) machine.HandleKey(c);
  machine.HandleKey(';');
  // 宇(nnzhs) starts with "nnz"
  machine.HandleKey('n');
  machine.HandleKey('n');
  auto snap = machine.HandleKey('z');
  bool found_yu = false;
  for (const auto& c : snap.candidates) {
    if (c == "宇") found_yu = true;
  }
  CHECK(found_yu);
}

TEST_CASE("Two-segment strokes compose virtual word from single chars", "[multi_syllable]") {
  TestFixture f;
  // Rime returns only single characters — no "宇骞" word.
  // 宇=nnzhs, 骞=nnzhhsshpnzzh  →  segments "nnz;nnz" each prefix-match.
  // Composition should build "宇骞" from the singles.
  FakeSession session({
      {"yuqian", {"宇", "骞"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("yuqian")) machine.HandleKey(c);
  machine.HandleKey(';');   // segment 1
  machine.HandleKey('n');
  machine.HandleKey('n');
  machine.HandleKey('z');   // → segment ["nnz"]
  machine.HandleKey(';');   // segment 2
  machine.HandleKey('n');
  machine.HandleKey('n');
  auto snap = machine.HandleKey('z');   // → segments ["nnz","nnz"]
  bool found_word = false;
  for (const auto& c : snap.candidates) {
    if (c == "宇骞") found_word = true;
  }
  CHECK(found_word);
}

// ==========================================================================
// Consistent candidate order (issue: sui and sui; should match)
// ==========================================================================

TEST_CASE("Pinyin and stroke phases produce same candidate order", "[ordering]") {
  TestFixture f;
  // 岁(300), 随(400), 碎(1500) — frequency sort should apply in both phases
  FakeSession session({
      {"sui", {"随", "岁", "碎"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("sui")) machine.HandleKey(c);
  auto pinyin_snap = machine.Snapshot();

  machine.HandleKey(';');
  auto stroke_snap = machine.Snapshot();

  CHECK(pinyin_snap.candidates == stroke_snap.candidates);
  CHECK(pinyin_snap.candidates[0] == "岁");
  CHECK(pinyin_snap.candidates[1] == "随");
  CHECK(pinyin_snap.candidates[2] == "碎");
}

// ==========================================================================
// Chinese punctuation (issue: comma and period)
// ==========================================================================

TEST_CASE("Comma from idle outputs Chinese comma", "[punctuation]") {
  TestFixture f;
  FakeSession session({});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  auto snap = machine.HandleKey(',');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "\xef\xbc\x8c");  // ，
}

TEST_CASE("Period from idle outputs Chinese period", "[punctuation]") {
  TestFixture f;
  FakeSession session({});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  auto snap = machine.HandleKey('.');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "\xe3\x80\x82");  // 。
}

TEST_CASE("Comma from pinyin commits top candidate then Chinese comma", "[punctuation]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  auto snap = machine.HandleKey(',');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "\xe4\xbd\xa0\xef\xbc\x8c");  // 你，
}

TEST_CASE("Period from stroke commits top candidate then Chinese period", "[punctuation]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  auto snap = machine.HandleKey('.');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "\xe4\xbd\xa0\xe3\x80\x82");  // 你。
}

TEST_CASE("Comma from selecting commits selected candidate then Chinese comma", "[punctuation]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  machine.HandleKey('J');  // → kSelecting, index=1 (尼 after sort)
  auto snap = machine.HandleKey(',');
  CHECK(snap.phase == Phase::kIdle);
  // 尼 + ，
  CHECK(snap.commit_text == "\xe5\xb0\xbc\xef\xbc\x8c");
}

// ==========================================================================
// All punctuation marks
// ==========================================================================

TEST_CASE("All punctuation from idle outputs Chinese equivalents", "[punctuation]") {
  TestFixture f;
  FakeSession session({});
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  SECTION("exclamation") {
    auto snap = machine.HandleKey('!');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbc\x81");  // ！
  }
  SECTION("question") {
    auto snap = machine.HandleKey('?');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbc\x9f");  // ？
  }
  SECTION("colon") {
    auto snap = machine.HandleKey(':');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbc\x9a");  // ：
  }
  SECTION("semicolon from idle") {
    auto snap = machine.HandleKey(';');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbc\x9b");  // ；
  }
  SECTION("backslash dunhao") {
    auto snap = machine.HandleKey('\\');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xe3\x80\x81");  // 、
  }
  SECTION("left paren") {
    auto snap = machine.HandleKey('(');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbc\x88");  // （
  }
  SECTION("right paren") {
    auto snap = machine.HandleKey(')');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbc\x89");  // ）
  }
  SECTION("left bracket") {
    auto snap = machine.HandleKey('[');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xe3\x80\x90");  // 【
  }
  SECTION("right bracket") {
    auto snap = machine.HandleKey(']');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xe3\x80\x91");  // 】
  }
  SECTION("left angle") {
    auto snap = machine.HandleKey('<');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xe3\x80\x8a");  // 《
  }
  SECTION("right angle") {
    auto snap = machine.HandleKey('>');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xe3\x80\x8b");  // 》
  }
  SECTION("tilde") {
    auto snap = machine.HandleKey('~');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "\xef\xbd\x9e");  // ～
  }
}

TEST_CASE("Exclamation from pinyin commits top candidate then Chinese exclamation", "[punctuation]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  auto snap = machine.HandleKey('!');
  CHECK(snap.phase == Phase::kIdle);
  CHECK(snap.commit_text == "\xe4\xbd\xa0\xef\xbc\x81");  // 你！
}

TEST_CASE("Semicolon from pinyin enters stroke phase not punctuation", "[punctuation]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  auto snap = machine.HandleKey(';');
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.commit_text.empty());
}

TEST_CASE("Semicolon from stroke phase advances segment not punctuation", "[punctuation]") {
  TestFixture f;
  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → stroke
  auto snap = machine.HandleKey(';');  // → next segment
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.stroke_buffer == ";");
  CHECK(snap.commit_text.empty());
}

// ==========================================================================
// Apostrophe syllable separator
// ==========================================================================

TEST_CASE("Single syllable pinyin filters out multi-char candidates", "[apostrophe]") {
  TestFixture f;
  FakeSession session({
      {"niao", {"尼奥", "鸟", "尿"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("niao")) machine.HandleKey(c);
  auto snap = machine.Snapshot();
  for (const auto& c : snap.candidates) {
    CHECK(StrokeFilter::SplitUtf8(c).size() == 1);
  }
  bool found_niao = false;
  for (const auto& c : snap.candidates) {
    if (c == "鸟") found_niao = true;
  }
  CHECK(found_niao);
}

TEST_CASE("Apostrophe enables multi-char word candidates", "[apostrophe]") {
  TestFixture f;
  FakeSession session({
      {"ni'ao", {"尼奥", "尼", "奥"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("ni")) machine.HandleKey(c);
  machine.HandleKey('\'');
  for (char c : std::string("ao")) machine.HandleKey(c);
  auto snap = machine.Snapshot();
  bool found_word = false;
  for (const auto& c : snap.candidates) {
    if (c == "尼奥") found_word = true;
  }
  CHECK(found_word);
}

TEST_CASE("Multi-syllable without apostrophe still shows words", "[apostrophe]") {
  TestFixture f;
  FakeSession session({
      {"zhongguo", {"中国", "中", "种"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhongguo")) machine.HandleKey(c);
  auto snap = machine.Snapshot();
  bool found_word = false;
  for (const auto& c : snap.candidates) {
    if (c == "中国") found_word = true;
  }
  CHECK(found_word);
}

// ==========================================================================
// Exact stroke match priority
// ==========================================================================

TEST_CASE("Exact stroke match ranks above prefix match", "[stroke_priority]") {
  TestFixture f;
  // 十 strokes="hs" (exact match for "hs"), rank=120
  // 事 strokes="hszhzhhz" (prefix match "hs"), rank=60 (higher freq)
  FakeSession session({
      {"shi", {"事", "十"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("shi")) machine.HandleKey(c);
  machine.HandleKey(';');
  machine.HandleKey('h');
  auto snap = machine.HandleKey('s');
  REQUIRE(snap.candidates.size() >= 2);
  CHECK(snap.candidates[0] == "十");
  CHECK(snap.candidates[1] == "事");
}

// ==========================================================================
// Partial pinyin commitment
// ==========================================================================

TEST_CASE("Partial commit returns to stroke phase with remaining pinyin", "[partial_commit]") {
  TestFixture f;
  // "qianshan" decomposes as "qian" + "shan"
  // Commit 千 (qian) → remaining "shan", enter stroke phase
  FakeSession session({
      {"qianshan", {"千山", "千", "山"}},
      {"shan", {"山"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("qianshan")) machine.HandleKey(c);
  machine.HandleKey(';');
  machine.HandleKey('J');  // select index 1 = 千 (words first: 千山, then singles: 千(350), 山(400))
  auto snap = machine.HandleKey(' ');
  CHECK(snap.commit_text == "千");
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.pinyin_buffer == "shan");
  bool found_shan = false;
  for (const auto& c : snap.candidates) {
    if (c == "山") found_shan = true;
  }
  CHECK(found_shan);
}

TEST_CASE("Full word commit returns to idle", "[partial_commit]") {
  TestFixture f;
  FakeSession session({
      {"qianshan", {"千山", "千", "山"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("qianshan")) machine.HandleKey(c);
  auto snap = machine.HandleKey(' ');  // commit top = 千山
  CHECK(snap.commit_text == "千山");
  CHECK(snap.phase == Phase::kIdle);
}

// ==========================================================================
// Multi-reading characters (多音字) — alternate pinyin input
// ==========================================================================

TEST_CASE("de/di character can be typed as both de and di", "[multi_reading]") {
  TestFixture f;
  FakeSession session({
      {"de", {"的", "得"}},
      {"di", {"的", "地"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path,
                             f.pinyin_dict_path));

  SECTION("de returns 的") {
    for (char c : std::string("de")) machine.HandleKey(c);
    machine.HandleKey(';');
    auto snap = machine.Snapshot();
    bool found = false;
    for (const auto& c : snap.candidates) {
      if (c == "的") found = true;
    }
    CHECK(found);
  }

  SECTION("di returns 的") {
    for (char c : std::string("di")) machine.HandleKey(c);
    machine.HandleKey(';');
    auto snap = machine.Snapshot();
    bool found = false;
    for (const auto& c : snap.candidates) {
      if (c == "的") found = true;
    }
    CHECK(found);
  }
}

TEST_CASE("chen/shen character can be typed as both chen and shen", "[multi_reading]") {
  TestFixture f;
  FakeSession session({
      {"c", {"沈"}}, {"ch", {"沈"}}, {"che", {"沈"}}, {"chen", {"沈", "陈"}},
      {"s", {"沈"}}, {"sh", {"沈"}}, {"she", {"沈"}}, {"shen", {"沈", "深"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path,
                             f.pinyin_dict_path));

  SECTION("chen returns 沈") {
    for (char c : std::string("chen")) machine.HandleKey(c);
    machine.HandleKey(';');
    auto snap = machine.Snapshot();
    bool found = false;
    for (const auto& c : snap.candidates) {
      if (c == "沈") found = true;
    }
    CHECK(found);
  }

  SECTION("shen returns 沈") {
    for (char c : std::string("shen")) machine.HandleKey(c);
    machine.HandleKey(';');
    auto snap = machine.Snapshot();
    bool found = false;
    for (const auto& c : snap.candidates) {
      if (c == "沈") found = true;
    }
    CHECK(found);
  }
}

TEST_CASE("Multi-reading filtering in pinyin phase preserves alternate readings", "[multi_reading]") {
  TestFixture f;
  FakeSession session({
      {"d", {"的"}}, {"de", {"的", "得"}},
      {"di", {"的", "地"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path,
                             f.pinyin_dict_path));

  // During pinyin input, prefix matching should still allow 的 for "d"
  machine.HandleKey('d');
  auto snap = machine.Snapshot();
  bool found_de = false;
  for (const auto& c : snap.candidates) {
    if (c == "的") found_de = true;
  }
  CHECK(found_de);

  // Continue to "di" — 的 should still be present
  machine.HandleKey('i');
  snap = machine.Snapshot();
  bool found_di = false;
  for (const auto& c : snap.candidates) {
    if (c == "的") found_di = true;
  }
  CHECK(found_di);
}

TEST_CASE("zhong/chong character can be typed as both zhong and chong", "[multi_reading]") {
  TestFixture f;
  FakeSession session({
      {"zhong", {"重", "中", "种"}},
      {"chong", {"重", "虫"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path,
                             f.pinyin_dict_path));

  SECTION("zhong returns 重") {
    for (char c : std::string("zhong")) machine.HandleKey(c);
    machine.HandleKey(';');
    auto snap = machine.Snapshot();
    bool found = false;
    for (const auto& c : snap.candidates) {
      if (c == "重") found = true;
    }
    CHECK(found);
  }

  SECTION("chong returns 重") {
    for (char c : std::string("chong")) machine.HandleKey(c);
    machine.HandleKey(';');
    auto snap = machine.Snapshot();
    bool found = false;
    for (const auto& c : snap.candidates) {
      if (c == "重") found = true;
    }
    CHECK(found);
  }
}

// ==========================================================================
// TAB stroke autocomplete
// ==========================================================================

TEST_CASE("TAB autocompletes shared strokes between top 2 candidates", "[tab_autocomplete]") {
  TestFixture f;
  // 努=zphznzp (rank 1081), 怒=zphznnznn (rank 1143)
  // Common prefix of remaining strokes: "zphzn"
  FakeSession session({
      {"nu", {"努", "怒"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("nu")) machine.HandleKey(c);
  machine.HandleKey(';');
  auto snap = machine.Snapshot();
  REQUIRE(snap.candidates.size() == 2);
  CHECK(snap.candidates[0] == "努");
  CHECK(snap.candidates[1] == "怒");

  snap = machine.HandleKey('\t');
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.stroke_buffer == "zphzn");
  REQUIRE(snap.candidates.size() == 2);
  CHECK(snap.candidates[0] == "努");
  CHECK(snap.candidates[1] == "怒");
}

TEST_CASE("TAB with single candidate autocompletes all remaining strokes", "[tab_autocomplete]") {
  TestFixture f;
  // 十=hs (rank 120)
  FakeSession session({
      {"shi", {"十"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("shi")) machine.HandleKey(c);
  machine.HandleKey(';');
  auto snap = machine.Snapshot();
  REQUIRE(snap.candidates.size() == 1);

  snap = machine.HandleKey('\t');
  CHECK(snap.stroke_buffer == "hs");
  REQUIRE(snap.candidates.size() == 1);
  CHECK(snap.candidates[0] == "十");
}

TEST_CASE("TAB does nothing when top 2 candidates diverge immediately", "[tab_autocomplete]") {
  TestFixture f;
  // 中=szhs (rank 54), 重=phszhhhsh (rank 233) — no shared prefix
  FakeSession session({
      {"zhong", {"中", "重"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("zhong")) machine.HandleKey(c);
  machine.HandleKey(';');
  auto snap = machine.Snapshot();
  CHECK(snap.stroke_buffer.empty());

  snap = machine.HandleKey('\t');
  CHECK(snap.stroke_buffer.empty());
}

TEST_CASE("TAB after partial strokes extends with remaining shared prefix", "[tab_autocomplete]") {
  TestFixture f;
  // 努=zphznzp, 怒=zphznnznn — after "z", remaining: "phznzp" vs "phznnznn"
  // Common prefix of remaining: "phzn"
  FakeSession session({
      {"nu", {"努", "怒"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(f.prism_path, f.stroke_dict_path, f.hanzi_db_path));

  for (char c : std::string("nu")) machine.HandleKey(c);
  machine.HandleKey(';');
  machine.HandleKey('z');
  auto snap = machine.HandleKey('\t');
  CHECK(snap.stroke_buffer == "zphzn");
}

}  // namespace predictable_pinyin::test
