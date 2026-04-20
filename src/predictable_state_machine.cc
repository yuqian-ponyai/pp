#include "predictable_state_machine.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace predictable_pinyin {

PredictableStateMachine::PredictableStateMachine(Session* session)
    : session_(session) {}

bool PredictableStateMachine::Initialize(const std::filesystem::path& prism_path,
                                        const std::filesystem::path& stroke_dict_path,
                                        const std::filesystem::path& hanzi_db_path,
                                        const std::filesystem::path& pinyin_dict_path) {
  return trie_.LoadFromPrismFile(prism_path) &&
         stroke_filter_.LoadFromStrokeDict(stroke_dict_path) &&
         frequency_sorter_.LoadFromCsv(hanzi_db_path) &&
         frequency_sorter_.LoadSupplementaryPinyin(pinyin_dict_path);
}

StateSnapshot PredictableStateMachine::HandleKey(char key) {
  if (IsPunctuationKey(key) && (key != ';' || phase_ == Phase::kIdle)) {
    return HandlePunctuation(key);
  }

  if (key == '\b' || key == 127) {
    switch (phase_) {
      case Phase::kIdle:
        break;
      case Phase::kPinyinInput:
        if (!pinyin_buffer_.empty()) {
          pinyin_buffer_.pop_back();
          session_->ProcessBackspace();
          if (pinyin_buffer_.empty()) {
            phase_ = Phase::kIdle;
          }
        }
        break;
      case Phase::kStrokeInput:
        if (!stroke_segments_.empty() && !stroke_segments_.back().empty()) {
          stroke_segments_.back().pop_back();
        } else if (stroke_segments_.size() > 1) {
          stroke_segments_.pop_back();
        } else {
          stroke_segments_.clear();
          phase_ = pinyin_buffer_.empty() ? Phase::kIdle : Phase::kPinyinInput;
        }
        break;
      case Phase::kSelecting:
        if (!selection_history_.empty()) {
          selected_index_ = selection_history_.back();
          selection_history_.pop_back();
        } else {
          phase_ = Phase::kStrokeInput;
        }
        break;
    }
    return Snapshot();
  }

  if (phase_ == Phase::kIdle && IsPinyinLetter(key)) {
    phase_ = Phase::kPinyinInput;
  }

  if (phase_ == Phase::kPinyinInput) {
    if (IsPinyinLetter(key) || key == '\'') {
      const char ch = (key == '\'')
                          ? '\''
                          : static_cast<char>(
                                std::tolower(static_cast<unsigned char>(key)));
      pinyin_buffer_.push_back(ch);
      session_->ProcessAsciiKey(ch);
      return Snapshot();
    }
    if (key == ';') {
      EnterStrokeInput();
      return Snapshot();
    }
    if (key == ' ') {
      return CommitTopCandidate();
    }
    return Snapshot();
  }

  if (phase_ == Phase::kStrokeInput) {
    if (IsStrokeKey(key)) {
      const char stroke = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
      if (!stroke_segments_.empty()) {
        stroke_segments_.back().push_back(stroke);
      }
      return Snapshot();
    }
    if (key == '\t') {
      return HandleTabAutocomplete();
    }
    if (key == ';') {
      stroke_segments_.emplace_back();
      return Snapshot();
    }
    if (IsSelectionKey(key)) {
      const auto candidates = CurrentCandidates();
      if (candidates.size() <= 1) {
        return Snapshot();
      }
      phase_ = Phase::kSelecting;
      selected_index_ = 0;
      selection_history_.clear();
      ApplySelectionDelta(SelectionDelta(key));
      return Snapshot();
    }
    if (key == ' ') {
      return CommitTopCandidate();
    }
    return Snapshot();
  }

  if (phase_ == Phase::kSelecting) {
    switch (ToUpperAscii(key)) {
      case 'J':
        ApplySelectionDelta(1);
        break;
      case 'K':
        ApplySelectionDelta(2);
        break;
      case 'L':
        ApplySelectionDelta(4);
        break;
      case 'F':
        ApplySelectionDelta(10);
        break;
      default:
        if (key == ' ') {
          const auto candidates = CurrentCandidates();
          const std::string committed =
              (selected_index_ >= 0 &&
               static_cast<std::size_t>(selected_index_) < candidates.size())
                  ? candidates[selected_index_]
                  : std::string{};
          return CommitWithContinuation(committed);
        }
        break;
    }
    return Snapshot();
  }

  return Snapshot();
}

std::string PredictableStateMachine::StrokeDisplayString() const {
  std::string result;
  for (std::size_t i = 0; i < stroke_segments_.size(); ++i) {
    if (i > 0) result += ';';
    result += stroke_segments_[i];
  }
  return result;
}

StateSnapshot PredictableStateMachine::Snapshot() const {
  StateSnapshot snapshot;
  snapshot.phase = phase_;
  snapshot.pinyin_buffer = pinyin_buffer_;
  snapshot.stroke_buffer = StrokeDisplayString();
  snapshot.selected_index = selected_index_;
  snapshot.selection_history = selection_history_;
  snapshot.hint = BuildHint();

  const RimeContextSnapshot rime_snapshot = session_->Snapshot();
  snapshot.raw_input = rime_snapshot.input;
  snapshot.preedit = rime_snapshot.preedit;
  snapshot.candidates = CurrentCandidates();
  snapshot.candidate_labels = BuildCandidateLabels(snapshot.candidates);
  return snapshot;
}

StateSnapshot PredictableStateMachine::Reset() {
  ResetLocalState();
  return Snapshot();
}

bool PredictableStateMachine::IsPinyinLetter(char key) {
  return key >= 'a' && key <= 'z';
}

bool PredictableStateMachine::IsStrokeKey(char key) {
  const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
  return lower == 'h' || lower == 's' || lower == 'p' || lower == 'n' ||
         lower == 'd' || lower == 'z';
}

char PredictableStateMachine::ToUpperAscii(char key) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
}

void PredictableStateMachine::ResetLocalState() {
  phase_ = Phase::kIdle;
  pinyin_buffer_.clear();
  stroke_segments_.clear();
  selected_index_ = 0;
  selection_history_.clear();
  session_->ClearComposition();
}

void PredictableStateMachine::EnterStrokeInput() {
  phase_ = Phase::kStrokeInput;
  stroke_segments_.clear();
  stroke_segments_.emplace_back();
  selected_index_ = 0;
  selection_history_.clear();
}

StateSnapshot PredictableStateMachine::CommitTopCandidate() {
  const auto candidates = CurrentCandidates();
  const std::string committed =
      candidates.empty() ? std::string{} : candidates[0];
  return CommitWithContinuation(committed);
}

StateSnapshot PredictableStateMachine::HandleTabAutocomplete() {
  if (stroke_segments_.empty()) return Snapshot();
  const auto candidates = CurrentCandidates();
  if (candidates.empty()) return Snapshot();

  const std::string r0 = stroke_filter_.RemainingStrokesForSegment(
      candidates[0], stroke_segments_);
  if (r0.empty()) return Snapshot();

  std::string common = r0;
  if (candidates.size() >= 2) {
    const std::string r1 = stroke_filter_.RemainingStrokesForSegment(
        candidates[1], stroke_segments_);
    std::size_t len = std::min(common.size(), r1.size());
    std::size_t i = 0;
    while (i < len && common[i] == r1[i]) ++i;
    common = common.substr(0, i);
  }

  if (!common.empty()) {
    stroke_segments_.back() += common;
  }
  return Snapshot();
}

bool PredictableStateMachine::IsSelectionKey(char key) {
  switch (ToUpperAscii(key)) {
    case 'J': case 'K': case 'L': case 'F':
      return true;
    default:
      return false;
  }
}

int PredictableStateMachine::SelectionDelta(char key) {
  switch (ToUpperAscii(key)) {
    case 'J': return 1;
    case 'K': return 2;
    case 'L': return 4;
    case 'F': return 10;
    default:  return 0;
  }
}

std::string PredictableStateMachine::BuildHint() const {
  switch (phase_) {
    case Phase::kIdle:
      return "Type a-z to start pinyin.";
    case Phase::kPinyinInput:
      return "Type pinyin letters. ; enters strokes. SPACE commits top. BACKSPACE deletes.";
    case Phase::kStrokeInput: {
      const std::size_t char_pos = stroke_segments_.size();
      return "Char " + std::to_string(char_pos) +
             " strokes. h/s/p/n/d/z adds stroke. TAB autocompletes."
             " ; next char. J/K/L/F selects. SPACE commits top.";
    }
    case Phase::kSelecting: {
      constexpr int kPageSize = 10;
      const auto candidates = CurrentCandidates();
      const int count = static_cast<int>(candidates.size());
      const int page_local = (selected_index_ % kPageSize) + 1;
      std::string hint = "SPACE commits [" + std::to_string(page_local) + "]";
      auto target = [&](const char* label, int delta) {
        const int dest = std::min(selected_index_ + delta, count - 1);
        if (dest != selected_index_ && dest < count) {
          hint += "  ";
          hint += label;
          hint += "\xe2\x86\x92";
          hint += std::to_string((dest % kPageSize) + 1);
        }
      };
      target("J", 1);
      target("K", 2);
      target("L", 4);
      {
        const int dest = std::min(selected_index_ + 10, count - 1);
        if (dest != selected_index_ && dest < count)
          hint += "  F\xe2\x86\x92next page";
      }
      if (!selection_history_.empty()) {
        hint += "  BACKSPACE undo";
      }
      return hint;
    }
  }
  return {};
}

std::vector<std::string> PredictableStateMachine::ComposeVirtualCandidates(
    const std::vector<std::string>& /*raw_candidates*/) const {
  if (stroke_segments_.size() < 2) return {};
  std::string clean_pinyin;
  for (char c : pinyin_buffer_)
    if (c != '\'') clean_pinyin += c;
  const auto syllables =
      trie_.Decompose(clean_pinyin, stroke_segments_.size());
  if (syllables.size() != stroke_segments_.size()) return {};

  // For each segment position, look up ALL characters matching the syllable
  // from hanziDB (not just Rime's candidates — Rime may omit characters for
  // non-first syllable positions), then filter by stroke prefix.
  std::vector<std::vector<std::string>> per_pos(stroke_segments_.size());
  for (std::size_t i = 0; i < stroke_segments_.size(); ++i) {
    for (const auto& ch :
         frequency_sorter_.CharactersForSyllable(syllables[i])) {
      if (stroke_filter_.HasStrokePrefix(ch, stroke_segments_[i]))
        per_pos[i].push_back(ch);
    }
    if (per_pos[i].empty()) return {};
  }

  // Build composed candidates: fix positions 0..N-2 to their top char, vary
  // the last position to give the user choices for the character they're
  // currently narrowing.
  std::string prefix;
  for (std::size_t i = 0; i + 1 < per_pos.size(); ++i) prefix += per_pos[i][0];

  std::vector<std::string> composed;
  for (const auto& ch : per_pos.back()) composed.push_back(prefix + ch);
  return composed;
}

std::vector<std::string> PredictableStateMachine::CurrentCandidates() const {
  const auto raw = session_->GetCandidates(64);
  auto candidates = stroke_filter_.Filter(raw, stroke_segments_);

  // Add composed virtual candidates for multi-segment strokes.
  const auto composed = ComposeVirtualCandidates(raw);
  for (const auto& c : composed) {
    if (std::find(candidates.begin(), candidates.end(), c) == candidates.end())
      candidates.push_back(c);
  }

  if (!pinyin_buffer_.empty()) {
    const bool has_apostrophe =
        pinyin_buffer_.find('\'') != std::string::npos;
    std::string clean_pinyin;
    if (has_apostrophe) {
      for (char c : pinyin_buffer_)
        if (c != '\'') clean_pinyin += c;
    } else {
      clean_pinyin = pinyin_buffer_;
    }
    const bool whole_is_syllable =
        !has_apostrophe && trie_.Contains(clean_pinyin);

    // When the entire input is a single valid syllable (no apostrophe),
    // filter out multi-char candidates — the user must use apostrophe to
    // explicitly request word decomposition.
    if (whole_is_syllable) {
      candidates.erase(
          std::remove_if(candidates.begin(), candidates.end(),
                         [](const std::string& c) {
                           return StrokeFilter::SplitUtf8(c).size() > 1;
                         }),
          candidates.end());
    }

    const bool is_complete = trie_.Contains(clean_pinyin);
    const bool is_prefix = !is_complete && trie_.HasExtension(clean_pinyin);
    if (is_complete || is_prefix) {
      const bool prefix = phase_ == Phase::kPinyinInput || is_prefix;
      candidates.erase(
          std::remove_if(candidates.begin(), candidates.end(),
                         [this, prefix, &clean_pinyin](const std::string& c) {
                           if (StrokeFilter::SplitUtf8(c).size() > 1)
                             return false;
                           return !frequency_sorter_.MatchesPinyin(
                               c, clean_pinyin, prefix);
                         }),
          candidates.end());
    }
  }
  std::vector<std::string> words;
  std::vector<std::string> singles;
  for (auto& c : candidates) {
    if (StrokeFilter::SplitUtf8(c).size() > 1) {
      words.push_back(std::move(c));
    } else {
      singles.push_back(std::move(c));
    }
  }
  // In stroke phase, exact stroke matches rank above prefix-only matches.
  const bool has_strokes = !stroke_segments_.empty() &&
                           !stroke_segments_[0].empty() &&
                           phase_ != Phase::kPinyinInput;
  if (has_strokes) {
    std::vector<std::string> exact, prefix_only;
    for (auto& s : singles) {
      if (stroke_filter_.IsExactStrokeMatch(s, stroke_segments_[0]))
        exact.push_back(std::move(s));
      else
        prefix_only.push_back(std::move(s));
    }
    frequency_sorter_.Sort(exact);
    frequency_sorter_.Sort(prefix_only);
    singles.clear();
    singles.insert(singles.end(), exact.begin(), exact.end());
    singles.insert(singles.end(), prefix_only.begin(), prefix_only.end());
  } else {
    frequency_sorter_.Sort(singles);
  }
  candidates.clear();
  candidates.insert(candidates.end(), words.begin(), words.end());
  candidates.insert(candidates.end(), singles.begin(), singles.end());
  return candidates;
}

std::vector<std::string> PredictableStateMachine::BuildCandidateLabels(
    const std::vector<std::string>& candidates) const {
  std::vector<std::string> labels(candidates.size());
  if (phase_ == Phase::kPinyinInput) {
    for (auto& label : labels) label = ";";
  } else if (phase_ == Phase::kStrokeInput) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      labels[i] = stroke_filter_.RemainingStrokesForSegment(
          candidates[i], stroke_segments_);
    }
  } else if (phase_ == Phase::kSelecting) {
    struct Nav { int delta; const char* key; };
    constexpr Nav kNavKeys[] = {{1, "J"}, {2, "K"}, {4, "L"}, {10, "F"}};
    const int count = static_cast<int>(candidates.size());
    for (const auto& nav : kNavKeys) {
      const int dest = selected_index_ + nav.delta;
      if (dest < count && dest != selected_index_) {
        labels[dest] = nav.key;
      }
    }
  }
  return labels;
}

bool PredictableStateMachine::IsPunctuationKey(char key) {
  switch (key) {
    case ',': case '.': case '!': case '?': case ':': case ';':
    case '\\': case '(': case ')': case '[': case ']':
    case '<': case '>': case '~':
      return true;
    default:
      return false;
  }
}

std::string PredictableStateMachine::ChinesePunctuation(char key) {
  switch (key) {
    case ',':  return "\xef\xbc\x8c";      // ，
    case '.':  return "\xe3\x80\x82";      // 。
    case '!':  return "\xef\xbc\x81";      // ！
    case '?':  return "\xef\xbc\x9f";      // ？
    case ':':  return "\xef\xbc\x9a";      // ：
    case ';':  return "\xef\xbc\x9b";      // ；
    case '\\': return "\xe3\x80\x81";      // 、
    case '(':  return "\xef\xbc\x88";      // （
    case ')':  return "\xef\xbc\x89";      // ）
    case '[':  return "\xe3\x80\x90";      // 【
    case ']':  return "\xe3\x80\x91";      // 】
    case '<':  return "\xe3\x80\x8a";      // 《
    case '>':  return "\xe3\x80\x8b";      // 》
    case '~':  return "\xef\xbd\x9e";      // ～
    default:   return {};
  }
}

StateSnapshot PredictableStateMachine::HandlePunctuation(char key) {
  const std::string punct = ChinesePunctuation(key);
  if (phase_ == Phase::kIdle) {
    StateSnapshot snapshot = Snapshot();
    snapshot.commit_text = punct;
    return snapshot;
  }
  if (phase_ == Phase::kSelecting) {
    const auto candidates = CurrentCandidates();
    const std::string committed =
        (selected_index_ >= 0 &&
         static_cast<std::size_t>(selected_index_) < candidates.size())
            ? candidates[selected_index_]
            : std::string{};
    auto snapshot = CommitWithContinuation(committed);
    snapshot.commit_text += punct;
    return snapshot;
  }
  auto snapshot = CommitTopCandidate();
  snapshot.commit_text += punct;
  return snapshot;
}

std::string PredictableStateMachine::ComputeRemainingPinyin(
    const std::string& committed) const {
  auto chars = StrokeFilter::SplitUtf8(committed);
  std::string remaining;
  for (char c : pinyin_buffer_)
    if (c != '\'') remaining += c;

  for (const auto& ch : chars) {
    bool found = false;
    for (std::size_t len = std::min(remaining.size(), std::size_t{6});
         len >= 1; --len) {
      std::string syllable = remaining.substr(0, len);
      if (trie_.Contains(syllable) &&
          frequency_sorter_.MatchesPinyin(ch, syllable, false)) {
        remaining = remaining.substr(len);
        found = true;
        break;
      }
    }
    if (!found) return {};
  }
  return remaining;
}

StateSnapshot PredictableStateMachine::CommitWithContinuation(
    const std::string& committed) {
  const std::string remaining = ComputeRemainingPinyin(committed);
  ResetLocalState();
  if (!remaining.empty()) {
    phase_ = Phase::kPinyinInput;
    for (char c : remaining) {
      pinyin_buffer_.push_back(c);
      session_->ProcessAsciiKey(c);
    }
    EnterStrokeInput();
  }
  StateSnapshot snapshot = Snapshot();
  snapshot.commit_text = committed;
  return snapshot;
}

void PredictableStateMachine::ApplySelectionDelta(int delta) {
  const int count = static_cast<int>(CurrentCandidates().size());
  if (count <= 0) {
    return;
  }
  selection_history_.push_back(selected_index_);
  selected_index_ = std::min(selected_index_ + delta, count - 1);
}

}  // namespace predictable_pinyin
