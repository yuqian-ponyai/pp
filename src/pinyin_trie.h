#ifndef PREDICTABLE_PINYIN_PINYIN_TRIE_H_
#define PREDICTABLE_PINYIN_PINYIN_TRIE_H_

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace predictable_pinyin {

class PinyinTrie {
 public:
  bool LoadFromPrismFile(const std::filesystem::path& path);
  bool Contains(const std::string& syllable) const;
  bool HasExtension(const std::string& prefix) const;
  bool ShouldAutoEnd(const std::string& syllable) const;

  // Splits a multi-syllable pinyin string into exactly target_count valid
  // syllables. Returns empty if no valid decomposition exists.
  std::vector<std::string> Decompose(const std::string& pinyin,
                                     std::size_t target_count) const;

 private:
  struct Node {
    bool accepting = false;
    std::array<std::unique_ptr<Node>, 26> children;
  };

  void Insert(const std::string& spelling);
  const Node* FindNode(const std::string& text) const;

  Node root_;
};

}  // namespace predictable_pinyin

#endif  // PREDICTABLE_PINYIN_PINYIN_TRIE_H_
