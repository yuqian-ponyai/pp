#include "frequency_sorter.h"

#include <algorithm>
#include <climits>
#include <fstream>

namespace predictable_pinyin {

namespace {

std::size_t Utf8CharByteLen(unsigned char first_byte) {
  if (first_byte < 0x80) return 1;
  if ((first_byte & 0xE0) == 0xC0) return 2;
  if ((first_byte & 0xF0) == 0xE0) return 3;
  if ((first_byte & 0xF8) == 0xF0) return 4;
  return 1;
}

}  // namespace

bool FrequencySorter::LoadFromCsv(const std::filesystem::path& csv_path) {
  std::ifstream file(csv_path);
  if (!file) return false;

  // Skip header: "frequency_rank,character,pinyin,..."
  std::string line;
  if (!std::getline(file, line)) return false;

  while (std::getline(file, line)) {
    // Format: rank,character,pinyin,...
    const auto first_comma = line.find(',');
    if (first_comma == std::string::npos) continue;
    const auto second_comma = line.find(',', first_comma + 1);
    if (second_comma == std::string::npos) continue;
    const auto third_comma = line.find(',', second_comma + 1);

    const int rank = std::atoi(line.substr(0, first_comma).c_str());
    std::string character = line.substr(first_comma + 1, second_comma - first_comma - 1);
    if (rank > 0 && !character.empty()) {
      ranks_.emplace(character, rank);
      if (third_comma != std::string::npos) {
        std::string pinyin = line.substr(second_comma + 1, third_comma - second_comma - 1);
        auto& readings = pinyin_map_[character];
        const std::string toneless = StripPinyinTones(pinyin);
        if (std::find(readings.begin(), readings.end(), toneless) == readings.end())
          readings.push_back(toneless);
      }
    }
  }
  return !ranks_.empty();
}

bool FrequencySorter::LoadSupplementaryPinyin(
    const std::filesystem::path& dict_path) {
  if (dict_path.empty()) return true;
  std::ifstream file(dict_path);
  if (!file) return false;

  // Skip YAML header (everything before the "..." line).
  std::string line;
  while (std::getline(file, line)) {
    if (line == "...") break;
  }

  // Each data line: character<TAB>pinyin<TAB>weight
  // Only single UTF-8 characters are relevant for multi-reading support.
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto tab1 = line.find('\t');
    if (tab1 == std::string::npos) continue;
    const auto tab2 = line.find('\t', tab1 + 1);

    std::string character = line.substr(0, tab1);
    if (FirstUtf8Char(character).size() != character.size()) continue;

    std::string pinyin = (tab2 != std::string::npos)
        ? line.substr(tab1 + 1, tab2 - tab1 - 1)
        : line.substr(tab1 + 1);

    auto& readings = pinyin_map_[character];
    if (std::find(readings.begin(), readings.end(), pinyin) == readings.end())
      readings.push_back(pinyin);
  }
  return true;
}

std::string FrequencySorter::StripPinyinTones(const std::string& pinyin) {
  // Maps toned vowel UTF-8 sequences to their base ASCII letter.
  // Each toned vowel is a 2-byte UTF-8 sequence (U+00xx or U+01xx range).
  struct ToneMap { const char* utf8; char base; };
  static const ToneMap kMap[] = {
      {"\xC4\x81", 'a'}, {"\xC3\xA1", 'a'}, {"\xC7\x8E", 'a'}, {"\xC3\xA0", 'a'},
      {"\xC4\x93", 'e'}, {"\xC3\xA9", 'e'}, {"\xC4\x9B", 'e'}, {"\xC3\xA8", 'e'},
      {"\xC4\xAB", 'i'}, {"\xC3\xAD", 'i'}, {"\xC7\x90", 'i'}, {"\xC3\xAC", 'i'},
      {"\xC5\x8D", 'o'}, {"\xC3\xB3", 'o'}, {"\xC7\x92", 'o'}, {"\xC3\xB2", 'o'},
      {"\xC5\xAB", 'u'}, {"\xC3\xBA", 'u'}, {"\xC7\x94", 'u'}, {"\xC3\xB9", 'u'},
      {"\xC7\x96", 'v'}, {"\xC7\x98", 'v'}, {"\xC7\x9A", 'v'}, {"\xC7\x9C", 'v'},
  };

  std::string result;
  std::size_t i = 0;
  while (i < pinyin.size()) {
    const auto lead = static_cast<unsigned char>(pinyin[i]);
    if (lead < 0x80) {
      result += pinyin[i++];
      continue;
    }
    const std::size_t len = Utf8CharByteLen(lead);
    bool matched = false;
    if (len == 2 && i + 1 < pinyin.size()) {
      for (const auto& m : kMap) {
        if (pinyin[i] == m.utf8[0] && pinyin[i + 1] == m.utf8[1]) {
          result += m.base;
          matched = true;
          break;
        }
      }
    }
    if (!matched) {
      result += pinyin.substr(i, std::min(len, pinyin.size() - i));
    }
    i += len;
  }
  return result;
}

bool FrequencySorter::MatchesPinyin(const std::string& character,
                                    const std::string& toneless_pinyin,
                                    bool prefix) const {
  const std::string key = FirstUtf8Char(character);
  const auto it = pinyin_map_.find(key);
  if (it == pinyin_map_.end()) return true;  // unknown characters pass through
  for (const auto& stored : it->second) {
    if (prefix) {
      if (stored.size() >= toneless_pinyin.size() &&
          stored.compare(0, toneless_pinyin.size(), toneless_pinyin) == 0)
        return true;
    } else {
      if (stored == toneless_pinyin) return true;
    }
  }
  return false;
}

std::vector<std::string> FrequencySorter::CharactersForSyllable(
    const std::string& toneless_pinyin) const {
  std::vector<std::string> result;
  for (const auto& [ch, readings] : pinyin_map_) {
    for (const auto& py : readings) {
      if (py == toneless_pinyin) { result.push_back(ch); break; }
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [this](const std::string& a, const std::string& b) {
                     return RankOf(a) < RankOf(b);
                   });
  return result;
}

int FrequencySorter::RankOf(const std::string& candidate) const {
  const auto it = ranks_.find(FirstUtf8Char(candidate));
  return (it != ranks_.end()) ? it->second : INT_MAX;
}

std::string FrequencySorter::FirstUtf8Char(const std::string& s) {
  if (s.empty()) return {};
  const std::size_t len = Utf8CharByteLen(static_cast<unsigned char>(s[0]));
  return s.substr(0, std::min(len, s.size()));
}

}  // namespace predictable_pinyin
