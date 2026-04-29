#include <catch2/catch_test_macros.hpp>

#include "frequency_sorter.h"
#include "predictable_state_machine.h"
#include "test_support.h"

namespace predictable_pinyin::test {

// Candidate order is owned by Rime. FrequencySorter supplies pinyin-reading
// metadata for filtering and virtual candidate synthesis.

TEST_CASE("FrequencySorter validates pinyin readings", "[frequency_sorter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const std::filesystem::path pinyin_dict_path = WriteSamplePinyinDict(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FrequencySorter sorter;
  REQUIRE(sorter.LoadFromCsv(hanzi_db_path));
  REQUIRE(sorter.LoadSupplementaryPinyin(pinyin_dict_path));

  CHECK(sorter.MatchesPinyin("重", "zhong"));
  CHECK(sorter.MatchesPinyin("重", "chong"));
  CHECK(sorter.MatchesPinyin("说", "yue"));
  CHECK_FALSE(sorter.MatchesPinyin("月", "shuo"));
}

TEST_CASE("PredictableStateMachine preserves Rime order end-to-end",
          "[frequency_sorter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FakeSession session({
      {"z", {"在", "中"}}, {"zh", {"中"}}, {"zho", {"中"}},
      {"zhon", {"中"}}, {"zhong", {"种", "重", "中"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path));

  for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
  machine.HandleKey(';');  // → kStrokeInput

  const StateSnapshot snapshot = machine.Snapshot();
  REQUIRE(snapshot.candidates.size() == 3);
  CHECK(snapshot.candidates[0] == "种");
  CHECK(snapshot.candidates[1] == "重");
  CHECK(snapshot.candidates[2] == "中");
}

TEST_CASE("Polyphone yue candidates keep Rime order", "[frequency_sorter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const std::filesystem::path pinyin_dict_path = WriteSamplePinyinDict(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  // 说 has a higher hanziDB frequency rank than 月, but Rime puts 月 first for
  // "yue". The state machine should not override that order.
  FakeSession session({
      {"y", {"月", "说"}}, {"yu", {"月", "说"}}, {"yue", {"月", "说"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path,
                             pinyin_dict_path));

  for (char c : {'y', 'u', 'e'}) machine.HandleKey(c);
  const StateSnapshot snapshot = machine.HandleKey(';');
  REQUIRE(snapshot.candidates.size() == 2);
  CHECK(snapshot.candidates[0] == "月");
  CHECK(snapshot.candidates[1] == "说");
}

}  // namespace predictable_pinyin::test
