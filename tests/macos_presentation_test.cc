#include <catch2/catch_test_macros.hpp>

#include "macos_presentation.h"

using namespace predictable_pinyin;

TEST_CASE("macOS marked text excludes instructions in every input phase",
          "[macos][presentation]") {
  StateSnapshot snap;
  snap.pinyin_buffer = "zhong";
  snap.stroke_buffer = "hs";
  snap.hint = "; enters strokes. SPACE commits top.";
  snap.candidate_labels = {"Space Commit"};
  for (const auto phase : {Phase::kIdle, Phase::kPinyinInput,
                           Phase::kStrokeInput, Phase::kSelecting}) {
    snap.phase = phase;
    const std::string expected = phase == Phase::kIdle ? "" :
        phase == Phase::kPinyinInput ? "zhong" : "zhong | hs";
    CHECK(BuildMacOSPreedit(snap) == expected);
    // Even a changed or stale UI hint cannot change the host composition.
    snap.hint = "different instructions";
    CHECK(BuildMacOSPreedit(snap) == expected);
  }
  snap.phase = Phase::kPinyinInput;
  snap.preedit = "zhong guo";
  CHECK(BuildMacOSPreedit(snap) == "zhong guo");
}

TEST_CASE("macOS candidate hints follow the phase and selection destinations",
          "[macos][presentation]") {
  StateSnapshot snap;
  CHECK(BuildMacOSHint(snap).empty());
  snap.phase = Phase::kPinyinInput;
  CHECK(BuildMacOSHint(snap).find("; Strokes") != std::string::npos);
  snap.phase = Phase::kStrokeInput;
  CHECK(BuildMacOSHint(snap).find("Tab Complete") != std::string::npos);
  snap.phase = Phase::kSelecting;
  for (const std::string hint : {"SPACE commits [2]  J→3  K→4",
                                 "SPACE commits [3]  J→4  BACKSPACE undo"}) {
    snap.hint = hint;
    CHECK(BuildMacOSHint(snap) == hint);
  }
  snap.phase = Phase::kIdle;
  CHECK(BuildMacOSHint(snap).empty());
}
