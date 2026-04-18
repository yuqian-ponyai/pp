#include "stroke_filter.h"

#include <algorithm>
#include <fstream>

namespace predictable_pinyin {

std::size_t StrokeFilter::Utf8CharByteLen(unsigned char first_byte) {
  if (first_byte < 0x80) return 1;
  if ((first_byte & 0xE0) == 0xC0) return 2;
  if ((first_byte & 0xF0) == 0xE0) return 3;
  if ((first_byte & 0xF8) == 0xF0) return 4;
  return 1;
}

std::vector<std::string> StrokeFilter::SplitUtf8(const std::string& s) {
  std::vector<std::string> chars;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t len = Utf8CharByteLen(static_cast<unsigned char>(s[i]));
    chars.push_back(s.substr(i, std::min(len, s.size() - i)));
    i += len;
  }
  return chars;
}

bool StrokeFilter::LoadFromStrokeDict(const std::filesystem::path& dict_path) {
  std::ifstream file(dict_path);
  if (!file) return false;

  bool in_data_section = false;
  std::string line;
  while (std::getline(file, line)) {
    if (!in_data_section) {
      if (line == "...") in_data_section = true;
      continue;
    }
    if (line.empty() || line[0] == '#') continue;

    const auto tab = line.find('\t');
    if (tab == std::string::npos) continue;

    std::string character = line.substr(0, tab);
    std::string strokes = line.substr(tab + 1);
    strokes_.emplace(std::move(character), std::move(strokes));
  }
  return !strokes_.empty();
}

bool StrokeFilter::HasStrokePrefix(const std::string& character,
                                   const std::string& prefix) const {
  if (prefix.empty()) return true;
  const auto it = strokes_.find(character);
  if (it == strokes_.end()) return false;
  return it->second.starts_with(prefix);
}

bool StrokeFilter::IsExactStrokeMatch(const std::string& character,
                                      const std::string& strokes) const {
  if (strokes.empty()) return false;
  const auto it = strokes_.find(character);
  if (it == strokes_.end()) return false;
  return it->second == strokes;
}

bool StrokeFilter::MatchesPerCharStrokes(
    const std::string& word,
    const std::vector<std::string>& segments) const {
  const auto chars = SplitUtf8(word);
  if (chars.size() < segments.size()) return false;

  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].empty()) continue;
    const auto it = strokes_.find(chars[i]);
    if (it == strokes_.end()) return false;
    if (!it->second.starts_with(segments[i])) return false;
  }
  return true;
}

std::vector<std::string> StrokeFilter::Filter(
    const std::vector<std::string>& candidates,
    const std::vector<std::string>& stroke_segments) const {
  const bool all_empty =
      stroke_segments.empty() ||
      std::all_of(stroke_segments.begin(), stroke_segments.end(),
                  [](const std::string& s) { return s.empty(); });
  if (all_empty) return candidates;

  std::vector<std::string> result;
  result.reserve(candidates.size());
  for (const auto& c : candidates) {
    if (MatchesPerCharStrokes(c, stroke_segments)) {
      result.push_back(c);
    }
  }
  return result;
}

std::string StrokeFilter::RemainingStrokesForSegment(
    const std::string& candidate,
    const std::vector<std::string>& stroke_segments) const {
  const auto chars = SplitUtf8(candidate);
  const std::size_t pos =
      stroke_segments.empty() ? 0 : stroke_segments.size() - 1;
  if (pos >= chars.size()) return {};
  const auto it = strokes_.find(chars[pos]);
  if (it == strokes_.end()) return {};
  const std::string& prefix =
      stroke_segments.empty() ? "" : stroke_segments[pos];
  if (prefix.size() >= it->second.size()) return {};
  if (!it->second.starts_with(prefix)) return {};
  return it->second.substr(prefix.size());
}

}  // namespace predictable_pinyin
