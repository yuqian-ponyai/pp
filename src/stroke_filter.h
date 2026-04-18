#ifndef PREDICTABLE_PINYIN_STROKE_FILTER_H_
#define PREDICTABLE_PINYIN_STROKE_FILTER_H_

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace predictable_pinyin {

// Loads the stroke sequence for each Chinese character from stroke.dict.yaml
// and filters candidate lists by per-character stroke segments typed by the
// user. Each segment corresponds to one character position in the candidate
// and is matched as a stroke prefix.
class StrokeFilter {
 public:
  bool LoadFromStrokeDict(const std::filesystem::path& dict_path);

  // Returns candidates that match every non-empty stroke segment against the
  // corresponding character position. When all segments are empty (or the
  // vector is empty), all candidates are returned unchanged.
  std::vector<std::string> Filter(
      const std::vector<std::string>& candidates,
      const std::vector<std::string>& stroke_segments) const;

  // Returns all remaining strokes for the character at the last segment
  // position in the candidate word.
  std::string RemainingStrokesForSegment(
      const std::string& candidate,
      const std::vector<std::string>& stroke_segments) const;

  // Returns true if the character's stroke sequence starts with prefix.
  bool HasStrokePrefix(const std::string& character,
                       const std::string& prefix) const;

  // Returns true if the character's stroke sequence exactly equals the strokes.
  bool IsExactStrokeMatch(const std::string& character,
                          const std::string& strokes) const;

  bool loaded() const { return !strokes_.empty(); }

  // Splits a UTF-8 string into individual characters.
  static std::vector<std::string> SplitUtf8(const std::string& s);

 private:
  static std::size_t Utf8CharByteLen(unsigned char first_byte);

  bool MatchesPerCharStrokes(
      const std::string& word,
      const std::vector<std::string>& segments) const;

  std::unordered_map<std::string, std::string> strokes_;
};

}  // namespace predictable_pinyin

#endif  // PREDICTABLE_PINYIN_STROKE_FILTER_H_
