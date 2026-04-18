#ifndef PREDICTABLE_PINYIN_STATE_MACHINE_H_
#define PREDICTABLE_PINYIN_STATE_MACHINE_H_

#include <filesystem>
#include <string>
#include <vector>

#include "frequency_sorter.h"
#include "pinyin_trie.h"
#include "rime_session.h"
#include "stroke_filter.h"

namespace predictable_pinyin {

enum class Phase {
  kIdle,
  kPinyinInput,
  kStrokeInput,
  kSelecting,
};

struct StateSnapshot {
  Phase phase = Phase::kIdle;
  std::string pinyin_buffer;
  std::string stroke_buffer;
  std::string raw_input;
  std::string preedit;
  std::string hint;
  std::string commit_text;
  int selected_index = 0;
  std::vector<int> selection_history;
  std::vector<std::string> candidates;
  std::vector<std::string> candidate_labels;
};

class PredictableStateMachine {
 public:
  explicit PredictableStateMachine(Session* session);

  bool Initialize(const std::filesystem::path& prism_path,
                  const std::filesystem::path& stroke_dict_path,
                  const std::filesystem::path& hanzi_db_path,
                  const std::filesystem::path& pinyin_dict_path = {});
  StateSnapshot HandleKey(char key);
  StateSnapshot Snapshot() const;
  StateSnapshot Reset();

 private:
  static bool IsPinyinLetter(char key);
  static bool IsStrokeKey(char key);
  static bool IsSelectionKey(char key);
  static bool IsPunctuationKey(char key);
  static char ToUpperAscii(char key);
  static int SelectionDelta(char key);
  static std::string ChinesePunctuation(char key);

  void ResetLocalState();
  void EnterStrokeInput();
  StateSnapshot CommitTopCandidate();
  StateSnapshot HandleTabAutocomplete();
  StateSnapshot HandlePunctuation(char key);
  std::string BuildHint() const;
  std::string StrokeDisplayString() const;
  std::vector<std::string> CurrentCandidates() const;
  std::vector<std::string> ComposeVirtualCandidates(
      const std::vector<std::string>& raw_candidates) const;
  std::string ComputeRemainingPinyin(const std::string& committed) const;
  StateSnapshot CommitWithContinuation(const std::string& committed);
  std::vector<std::string> BuildCandidateLabels(
      const std::vector<std::string>& candidates) const;
  void ApplySelectionDelta(int delta);

  Session* session_;
  PinyinTrie trie_;
  StrokeFilter stroke_filter_;
  FrequencySorter frequency_sorter_;
  Phase phase_ = Phase::kIdle;
  std::string pinyin_buffer_;
  std::vector<std::string> stroke_segments_;
  int selected_index_ = 0;
  std::vector<int> selection_history_;
};

}  // namespace predictable_pinyin

#endif  // PREDICTABLE_PINYIN_STATE_MACHINE_H_
