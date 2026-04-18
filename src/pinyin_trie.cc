#include "pinyin_trie.h"

#include <fstream>
#include <functional>
#include <sstream>

namespace predictable_pinyin {

bool PinyinTrie::LoadFromPrismFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream line_stream(line);
    std::string spelling;
    std::string value;
    std::getline(line_stream, spelling, '\t');
    std::getline(line_stream, value, '\t');

    // The prism file contains prefixes and derived spellings in the first
    // column. For pinyin auto-end we only want exact, canonical syllables.
    if (!spelling.empty() && spelling == value) {
      Insert(spelling);
    }
  }
  return true;
}

bool PinyinTrie::Contains(const std::string& syllable) const {
  const Node* node = FindNode(syllable);
  return node != nullptr && node->accepting;
}

bool PinyinTrie::HasExtension(const std::string& prefix) const {
  const Node* node = FindNode(prefix);
  if (node == nullptr) {
    return false;
  }
  for (const auto& child : node->children) {
    if (child != nullptr) {
      return true;
    }
  }
  return false;
}

bool PinyinTrie::ShouldAutoEnd(const std::string& syllable) const {
  return Contains(syllable) && !HasExtension(syllable);
}

void PinyinTrie::Insert(const std::string& spelling) {
  Node* node = &root_;
  for (char c : spelling) {
    if (c < 'a' || c > 'z') {
      return;
    }
    auto& child = node->children[c - 'a'];
    if (child == nullptr) {
      child = std::make_unique<Node>();
    }
    node = child.get();
  }
  node->accepting = true;
}

const PinyinTrie::Node* PinyinTrie::FindNode(const std::string& text) const {
  const Node* node = &root_;
  for (char c : text) {
    if (c < 'a' || c > 'z') {
      return nullptr;
    }
    const auto& child = node->children[c - 'a'];
    if (child == nullptr) {
      return nullptr;
    }
    node = child.get();
  }
  return node;
}

std::vector<std::string> PinyinTrie::Decompose(
    const std::string& pinyin, std::size_t target_count) const {
  std::vector<std::string> result;
  std::function<bool(std::size_t)> solve = [&](std::size_t pos) -> bool {
    if (pos == pinyin.size()) return result.size() == target_count;
    if (result.size() >= target_count) return false;
    const std::size_t remaining = pinyin.size() - pos;
    for (std::size_t len = std::min(remaining, std::size_t{6}); len >= 1;
         --len) {
      if (Contains(pinyin.substr(pos, len))) {
        result.push_back(pinyin.substr(pos, len));
        if (solve(pos + len)) return true;
        result.pop_back();
      }
    }
    return false;
  };
  solve(0);
  return result;
}

}  // namespace predictable_pinyin
