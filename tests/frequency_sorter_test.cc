#include <catch2/catch_test_macros.hpp>

#include "frequency_sorter.h"
#include "predictable_state_machine.h"
#include "test_support.h"

namespace predictable_pinyin::test {

// Sample ranks from WriteSampleHanziDb():
//   在(6) < 中(54) < 你(73) < 重(233) < 种(736) < 尼(1185)

TEST_CASE("FrequencySorter sorts candidates by frequency rank", "[frequency_sorter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FrequencySorter sorter;
  REQUIRE(sorter.LoadFromCsv(hanzi_db_path));

  SECTION("reorders candidates by ascending frequency rank") {
    std::vector<std::string> candidates = {"种", "中", "重"};
    sorter.Sort(candidates);
    REQUIRE(candidates == std::vector<std::string>{"中", "重", "种"});
  }

  SECTION("unknown characters are placed last, preserving relative order") {
    std::vector<std::string> candidates = {"X", "中", "Y"};
    sorter.Sort(candidates);
    // 中(54) first, then X and Y (both unknown) in original relative order
    CHECK(candidates[0] == "中");
    CHECK(candidates[1] == "X");
    CHECK(candidates[2] == "Y");
  }

  SECTION("stable sort preserves order among equal-rank candidates") {
    std::vector<std::string> candidates = {"A", "B", "C"};
    sorter.Sort(candidates);
    // All unknown → all get INT_MAX rank → original order preserved
    CHECK(candidates == std::vector<std::string>{"A", "B", "C"});
  }
}

TEST_CASE("FrequencySorter integrates with PredictableStateMachine end-to-end",
          "[frequency_sorter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  // FakeSession returns candidates in non-frequency order for "zhong".
  // After frequency sorting: 中(54) < 重(233) < 种(736)
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
  CHECK(snapshot.candidates[0] == "中");
  CHECK(snapshot.candidates[1] == "重");
  CHECK(snapshot.candidates[2] == "种");
}

}  // namespace predictable_pinyin::test
