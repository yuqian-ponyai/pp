#include <catch2/catch_test_macros.hpp>

#include "predictable_state_machine.h"
#include "stroke_filter.h"
#include "test_support.h"

namespace predictable_pinyin::test {

// Stroke sequences used throughout (from upstream stroke.dict.yaml):
//   中 szhs   种 phspnszhs   重 phszhhhsh   国 szzshh

TEST_CASE("StrokeFilter filters candidates by stroke prefix", "[stroke_filter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(prism_path.parent_path());
  const ScopedDirectoryCleanup cleanup(prism_path.parent_path());

  StrokeFilter filter;
  REQUIRE(filter.LoadFromStrokeDict(stroke_dict_path));

  const std::vector<std::string> candidates = {"中", "种", "重"};

  SECTION("empty segments returns all candidates unchanged") {
    CHECK(filter.Filter(candidates, {}) == candidates);
  }

  SECTION("single segment empty returns all candidates unchanged") {
    CHECK(filter.Filter(candidates, {""}) == candidates);
  }

  SECTION("single stroke narrows to matching chars") {
    const auto result = filter.Filter(candidates, {"s"});
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "中");
  }

  SECTION("multiple strokes narrow further") {
    REQUIRE(filter.Filter(candidates, {"ph"}).size() == 2);
    const auto only_zhong = filter.Filter(candidates, {"phsp"});
    REQUIRE(only_zhong.size() == 1);
    CHECK(only_zhong[0] == "种");
    const auto only_chong = filter.Filter(candidates, {"phsz"});
    REQUIRE(only_chong.size() == 1);
    CHECK(only_chong[0] == "重");
  }

  SECTION("candidates absent from the stroke dict are dropped") {
    const auto result = filter.Filter({"中", "X"}, {"s"});
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "中");
  }
}

TEST_CASE("StrokeFilter per-character matching for words", "[stroke_filter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(prism_path.parent_path());
  const ScopedDirectoryCleanup cleanup(prism_path.parent_path());

  StrokeFilter filter;
  REQUIRE(filter.LoadFromStrokeDict(stroke_dict_path));

  // 中国: 中=szhs 国=szzshh
  const std::vector<std::string> candidates = {"中国", "中", "种"};

  SECTION("single segment matches first char of word") {
    const auto result = filter.Filter(candidates, {"s"});
    REQUIRE(result.size() == 2);
    CHECK(result[0] == "中国");
    CHECK(result[1] == "中");
  }

  SECTION("two segments match both chars of word") {
    const auto result = filter.Filter(candidates, {"sz", "sz"});
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "中国");
  }

  SECTION("skip first char via empty segment") {
    const auto result = filter.Filter(candidates, {"", "sz"});
    REQUIRE(result.size() == 1);
    CHECK(result[0] == "中国");
  }

  SECTION("word too short for segments is excluded") {
    // 3 segments but 中 is only 1 char
    const auto result = filter.Filter(candidates, {"s", "s", "s"});
    CHECK(result.empty());
  }
}

TEST_CASE("StrokeFilter integrates with PredictableStateMachine stroke narrowing",
          "[stroke_filter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FakeSession session({
      {"z", {"在", "中"}}, {"zh", {"中"}}, {"zho", {"中"}},
      {"zhon", {"中"}}, {"zhong", {"中", "种", "重"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path));

  for (char c : {'z', 'h', 'o', 'n', 'g'}) machine.HandleKey(c);
  machine.HandleKey(';');  // → kStrokeInput

  const StateSnapshot before_stroke = machine.Snapshot();
  CHECK(before_stroke.phase == Phase::kStrokeInput);
  CHECK(before_stroke.candidates.size() == 3);

  const StateSnapshot after_s = machine.HandleKey('s');
  REQUIRE(after_s.candidates.size() == 1);
  CHECK(after_s.candidates[0] == "中");
}

TEST_CASE("StrokeFilter RemainingStrokesForSegment returns full remaining strokes",
          "[stroke_filter]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(prism_path.parent_path());
  const ScopedDirectoryCleanup cleanup(prism_path.parent_path());

  StrokeFilter filter;
  REQUIRE(filter.LoadFromStrokeDict(stroke_dict_path));

  SECTION("single character with single segment") {
    // 中=szhs, 种=phspnszhs, 重=phszhhhsh
    CHECK(filter.RemainingStrokesForSegment("中", {""}) == "szhs");
    CHECK(filter.RemainingStrokesForSegment("种", {""}) == "phspnszhs");
    CHECK(filter.RemainingStrokesForSegment("重", {""}) == "phszhhhsh");

    CHECK(filter.RemainingStrokesForSegment("中", {"s"}) == "zhs");
    CHECK(filter.RemainingStrokesForSegment("种", {"ph"}) == "spnszhs");
    CHECK(filter.RemainingStrokesForSegment("重", {"ph"}) == "szhhhsh");
    CHECK(filter.RemainingStrokesForSegment("种", {"phs"}) == "pnszhs");
    CHECK(filter.RemainingStrokesForSegment("重", {"phs"}) == "zhhhsh");
  }

  SECTION("word with multiple segments — last segment determines position") {
    // 中国: 中=szhs 国=szzshh
    CHECK(filter.RemainingStrokesForSegment("中国", {"sz", ""}) == "szzshh");
    CHECK(filter.RemainingStrokesForSegment("中国", {"sz", "s"}) == "zzshh");
  }

  SECTION("unknown character returns empty") {
    CHECK(filter.RemainingStrokesForSegment("X", {""}).empty());
  }

  SECTION("prefix at or beyond stroke length returns empty") {
    CHECK(filter.RemainingStrokesForSegment("中", {"szhs"}).empty());
  }
}

TEST_CASE("SplitUtf8 splits multi-byte strings correctly", "[stroke_filter]") {
  CHECK(StrokeFilter::SplitUtf8("中国") == std::vector<std::string>{"中", "国"});
  CHECK(StrokeFilter::SplitUtf8("A").size() == 1);
  CHECK(StrokeFilter::SplitUtf8("").empty());
  CHECK(StrokeFilter::SplitUtf8("中").size() == 1);
}

}  // namespace predictable_pinyin::test
