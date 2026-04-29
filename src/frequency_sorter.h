#ifndef PREDICTABLE_PINYIN_FREQUENCY_SORTER_H_
#define PREDICTABLE_PINYIN_FREQUENCY_SORTER_H_

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace predictable_pinyin {

// Loads character pinyin data from hanzi_db.csv and supplementary Rime data.
// Candidate order comes from Rime; ranks are only used when synthesizing virtual
// candidates that Rime did not provide.
class FrequencySorter {
 public:
  bool LoadFromCsv(const std::filesystem::path& csv_path);

  // Loads additional pinyin readings from a Rime pinyin_simp.dict.yaml file.
  // Only single-character entries are used; multi-character entries are skipped.
  bool LoadSupplementaryPinyin(const std::filesystem::path& dict_path);

  // Returns true if the character's known pinyin matches the given toneless
  // pinyin, or if the character is not in the database (pass-through).
  // When prefix=true, checks if the stored pinyin starts with the input
  // (for filtering during incremental pinyin typing).
  bool MatchesPinyin(const std::string& character,
                     const std::string& toneless_pinyin,
                     bool prefix = false) const;

  // Returns all characters whose toneless pinyin exactly matches the given
  // syllable, sorted by frequency rank.
  std::vector<std::string> CharactersForSyllable(
      const std::string& toneless_pinyin) const;

  bool loaded() const { return !ranks_.empty(); }

  static std::string StripPinyinTones(const std::string& pinyin);

 private:
  int RankOf(const std::string& candidate) const;
  static std::string FirstUtf8Char(const std::string& s);

  // Maps each character (UTF-8 byte string) to its 1-based frequency rank.
  std::unordered_map<std::string, int> ranks_;
  // Maps each character to all known tone-stripped pinyins (from hanzi_db.csv
  // plus any supplementary Rime dictionary data for multi-reading characters).
  std::unordered_map<std::string, std::vector<std::string>> pinyin_map_;
};

}  // namespace predictable_pinyin

#endif  // PREDICTABLE_PINYIN_FREQUENCY_SORTER_H_
