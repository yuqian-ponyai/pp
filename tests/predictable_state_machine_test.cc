#include <catch2/catch_test_macros.hpp>

#include "predictable_state_machine.h"
#include "test_support.h"

namespace predictable_pinyin::test {

TEST_CASE("PredictableStateMachine requires semicolon to enter stroke input",
          "[predictable_state_machine]") {
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
  auto snap = machine.Snapshot();
  CHECK(snap.phase == Phase::kPinyinInput);
  CHECK(snap.pinyin_buffer == "zhong");

  snap = machine.HandleKey(';');
  CHECK(snap.phase == Phase::kStrokeInput);
  CHECK(snap.pinyin_buffer == "zhong");
  CHECK(snap.raw_input == "zhong");
}

TEST_CASE("PredictableStateMachine undoes selection navigation before commit",
          "[predictable_state_machine]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "拟", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → kStrokeInput
  machine.HandleKey('J');  // → kSelecting, index=1
  const StateSnapshot undo_snapshot = machine.HandleKey('\b');
  const StateSnapshot commit_snapshot = machine.HandleKey(' ');

  CHECK(undo_snapshot.phase == Phase::kSelecting);
  CHECK(undo_snapshot.selected_index == 0);
  CHECK(commit_snapshot.phase == Phase::kIdle);
  CHECK(commit_snapshot.commit_text == "你");
}

TEST_CASE("PredictableStateMachine backspace crosses stroke and pinyin boundaries",
          "[predictable_state_machine]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "拟", "尼"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');
  const StateSnapshot back_to_pinyin = machine.HandleKey('\b');
  const StateSnapshot back_to_single_letter = machine.HandleKey('\b');

  CHECK(back_to_pinyin.phase == Phase::kPinyinInput);
  CHECK(back_to_single_letter.pinyin_buffer == "n");
}

TEST_CASE("PredictableStateMachine J/K/L/F from stroke enters selecting with delta",
          "[predictable_state_machine]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const auto& dir = prism_path.parent_path();
  const std::filesystem::path stroke_dict_path = WriteSampleStrokeDict(dir);
  const std::filesystem::path hanzi_db_path = WriteSampleHanziDb(dir);
  const ScopedDirectoryCleanup cleanup(dir);

  FakeSession session({
      {"n", {"你"}}, {"ni", {"你", "尼", "拟"}},
  });
  PredictableStateMachine machine(&session);
  REQUIRE(machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path));

  machine.HandleKey('n');
  machine.HandleKey('i');
  machine.HandleKey(';');  // → kStrokeInput

  SECTION("J enters selecting at index 1") {
    const auto snap = machine.HandleKey('J');
    CHECK(snap.phase == Phase::kSelecting);
    CHECK(snap.selected_index == 1);
  }

  SECTION("K enters selecting at index 2") {
    const auto snap = machine.HandleKey('K');
    CHECK(snap.phase == Phase::kSelecting);
    CHECK(snap.selected_index == 2);
  }

  SECTION("L clamps to last index") {
    const auto snap = machine.HandleKey('L');  // +4, but only 3 items → index 2
    CHECK(snap.phase == Phase::kSelecting);
    CHECK(snap.selected_index == 2);
  }

  SECTION("F clamps to last index") {
    const auto snap = machine.HandleKey('F');  // +10, but only 3 items → index 2
    CHECK(snap.phase == Phase::kSelecting);
    CHECK(snap.selected_index == 2);
  }

  SECTION("commit from selecting reached via stroke J") {
    machine.HandleKey('K');  // index 2
    const auto snap = machine.HandleKey(' ');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "拟");
  }

  SECTION("J/K/L/F ignored when ≤1 candidate") {
    FakeSession single_session({{"a", {"啊"}}});
    PredictableStateMachine m2(&single_session);
    REQUIRE(m2.Initialize(prism_path, stroke_dict_path, hanzi_db_path));
    m2.HandleKey('a');
    m2.HandleKey(';');  // → kStrokeInput with 1 candidate
    const auto snap = m2.HandleKey('J');
    CHECK(snap.phase == Phase::kStrokeInput);
  }

  SECTION("d is a distinct stroke key") {
    const auto snap = machine.HandleKey('d');
    CHECK(snap.phase == Phase::kStrokeInput);
    CHECK(snap.stroke_buffer == "d");
  }

  SECTION("t is not a stroke key (merged into d)") {
    const auto snap = machine.HandleKey('t');
    CHECK(snap.phase == Phase::kStrokeInput);
    CHECK(snap.stroke_buffer.empty());
  }

  SECTION("SPACE from stroke commits top candidate") {
    const auto snap = machine.HandleKey(' ');
    CHECK(snap.phase == Phase::kIdle);
    CHECK(snap.commit_text == "你");
  }

  SECTION("D in stroke is treated as stroke key, not selection key") {
    const auto snap = machine.HandleKey('D');
    CHECK(snap.phase == Phase::kStrokeInput);
    CHECK(snap.stroke_buffer == "d");
  }
}

}  // namespace predictable_pinyin::test
