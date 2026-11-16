#ifndef PREDICTABLE_PINYIN_MACOS_PRESENTATION_H_
#define PREDICTABLE_PINYIN_MACOS_PRESENTATION_H_

#include "predictable_state_machine.h"

namespace predictable_pinyin {

// Marked text belongs to the host document. Never include UI instructions.
inline std::string BuildMacOSPreedit(const StateSnapshot& snap) {
  if (snap.phase == Phase::kIdle) return {};
  std::string preedit = snap.preedit.empty() ? snap.pinyin_buffer : snap.preedit;
  if (snap.phase == Phase::kStrokeInput || snap.phase == Phase::kSelecting) {
    preedit += " | ";
    preedit += snap.stroke_buffer;
  }
  return preedit;
}

// Display only in the candidate window's footer, never as a candidate
// or marked text. Reuse selection hints so destinations stay in sync.
inline std::string BuildMacOSHint(const StateSnapshot& snap) {
  switch (snap.phase) {
    case Phase::kIdle:
      return {};
    case Phase::kPinyinInput:
      return "; Strokes · Space Commit · Backspace Delete";
    case Phase::kStrokeInput:
      return "Tab Complete strokes · ; Next character\n"
             "J/K/L/F Select · Space Commit · Backspace Delete";
    case Phase::kSelecting:
      return snap.hint;
  }
  return {};
}

}  // namespace predictable_pinyin

#endif  // PREDICTABLE_PINYIN_MACOS_PRESENTATION_H_
