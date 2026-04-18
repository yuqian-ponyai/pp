#include <catch2/catch_test_macros.hpp>

#include "pinyin_trie.h"
#include "test_support.h"

namespace predictable_pinyin::test {

TEST_CASE("PinyinTrie loads only canonical syllables from prism", "[pinyin_trie]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const ScopedDirectoryCleanup cleanup(prism_path.parent_path());

  PinyinTrie trie;
  REQUIRE(trie.LoadFromPrismFile(prism_path));

  CHECK(trie.Contains("ni"));
  CHECK(trie.Contains("nin"));
  CHECK(trie.Contains("zhong"));
  CHECK_FALSE(trie.Contains("zho"));
  CHECK_FALSE(trie.Contains("agn"));
}

TEST_CASE("PinyinTrie reports extension and auto-end correctly", "[pinyin_trie]") {
  const std::filesystem::path prism_path = WriteSamplePrism();
  const ScopedDirectoryCleanup cleanup(prism_path.parent_path());

  PinyinTrie trie;
  REQUIRE(trie.LoadFromPrismFile(prism_path));

  CHECK(trie.HasExtension("ni"));
  CHECK_FALSE(trie.HasExtension("zhong"));
  CHECK_FALSE(trie.HasExtension("missing"));

  CHECK_FALSE(trie.ShouldAutoEnd("ni"));
  CHECK(trie.ShouldAutoEnd("zhong"));
}

}  // namespace predictable_pinyin::test
