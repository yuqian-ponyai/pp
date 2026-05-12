#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef PP_SOURCE_DIR
#define PP_SOURCE_DIR "."
#endif

namespace {

std::string ReadSourceFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  REQUIRE(in.is_open());
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("macOS StateSnapshot ivar uses automatic Objective-C++ lifetime",
          "[macos][source-regression]") {
  const std::filesystem::path macos_plugin =
      std::filesystem::path(PP_SOURCE_DIR) / "src" / "macos_plugin.mm";
  const std::string source = ReadSourceFile(macos_plugin);

  // StateSnapshot is a C++ ivar in PPInputController, so Objective-C++ already
  // constructs and destroys it. Manual lifetime management caused double
  // destruction when IMK released a controller.
  CHECK(source.find("new (&_snapshot) StateSnapshot()") == std::string::npos);
  CHECK(source.find("_snapshot.~StateSnapshot()") == std::string::npos);
}
