#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "predictable_state_machine.h"

namespace {

using predictable_pinyin::Phase;
using predictable_pinyin::PredictableStateMachine;
using predictable_pinyin::RimeSession;
using predictable_pinyin::StateSnapshot;

class ScopedDirectoryCleanup {
 public:
  ScopedDirectoryCleanup() = default;
  explicit ScopedDirectoryCleanup(std::filesystem::path path)
      : path_(std::move(path)) {}

  ScopedDirectoryCleanup(const ScopedDirectoryCleanup&) = delete;
  ScopedDirectoryCleanup& operator=(const ScopedDirectoryCleanup&) = delete;

  ScopedDirectoryCleanup(ScopedDirectoryCleanup&& other) noexcept
      : path_(std::move(other.path_)) {
    other.path_.clear();
  }

  ScopedDirectoryCleanup& operator=(ScopedDirectoryCleanup&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Cleanup();
    path_ = std::move(other.path_);
    other.path_.clear();
    return *this;
  }

  ~ScopedDirectoryCleanup() {
    Cleanup();
  }

 private:
  void Cleanup() {
    if (path_.empty()) {
      return;
    }
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    path_.clear();
  }

  std::filesystem::path path_;
};

std::string GetEnvOrDefault(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string(fallback) : std::string(value);
}

bool EnvFlagEnabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }
  return std::string(value) == "1";
}

void RemoveCopiedUserDbLocks(const std::filesystem::path& user_data_dir) {
  for (const auto& entry : std::filesystem::recursive_directory_iterator(user_data_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().filename() == "LOCK") {
      std::error_code error;
      std::filesystem::remove(entry.path(), error);
    }
  }
}

std::filesystem::path CreateIsolatedUserDataDir(
    const std::filesystem::path& template_dir) {
  if (!std::filesystem::exists(template_dir)) {
    throw std::runtime_error("Missing user data template dir: " +
                             template_dir.string());
  }

  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path() / "predictable_pinyin_cli";
  std::filesystem::create_directories(temp_root);

  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::filesystem::path candidate =
        temp_root / ("run_" + std::to_string(timestamp) + "_" + std::to_string(attempt));
    if (std::filesystem::exists(candidate)) {
      continue;
    }
    std::filesystem::copy(template_dir, candidate,
                          std::filesystem::copy_options::recursive);
    RemoveCopiedUserDbLocks(candidate);
    return candidate;
  }

  throw std::runtime_error("Failed to create isolated test user data dir.");
}

std::string PhaseName(Phase phase) {
  switch (phase) {
    case Phase::kIdle:
      return "IDLE";
    case Phase::kPinyinInput:
      return "PINYIN_INPUT";
    case Phase::kStrokeInput:
      return "STROKE_INPUT";
    case Phase::kSelecting:
      return "SELECTING";
  }
  return "UNKNOWN";
}

char ParseKeyToken(const std::string& token) {
  if (token == "<space>") {
    return ' ';
  }
  if (token == "<backspace>") {
    return '\b';
  }
  if (token == "<tab>") {
    return '\t';
  }
  return token.empty() ? '\0' : token[0];
}

void PrintSnapshot(const StateSnapshot& snapshot) {
  std::cout << "phase: " << PhaseName(snapshot.phase) << '\n';
  std::cout << "pinyin_buffer: " << snapshot.pinyin_buffer << '\n';
  std::cout << "stroke_buffer: " << snapshot.stroke_buffer << '\n';
  std::cout << "raw_input: " << snapshot.raw_input << '\n';
  std::cout << "preedit: " << snapshot.preedit << '\n';
  std::cout << "selected_index: " << snapshot.selected_index << '\n';
  std::cout << "hint: " << snapshot.hint << '\n';
  if (!snapshot.commit_text.empty()) {
    std::cout << "commit: " << snapshot.commit_text << '\n';
  }
  std::cout << "candidates:";
  if (snapshot.candidates.empty()) {
    std::cout << " <none>";
  } else {
    for (std::size_t i = 0; i < snapshot.candidates.size(); ++i) {
      const std::string& label = i < snapshot.candidate_labels.size()
                                     ? snapshot.candidate_labels[i] : "";
      const bool selected = snapshot.phase == Phase::kSelecting &&
                            static_cast<int>(i) == snapshot.selected_index;
      if (selected) {
        std::cout << " [" << snapshot.candidates[i] << ']';
      } else if (!label.empty()) {
        std::cout << ' ' << label << ':' << snapshot.candidates[i];
      } else {
        std::cout << ' ' << snapshot.candidates[i];
      }
    }
  }
  std::cout << "\n\n";
}

void RunToken(PredictableStateMachine* machine, const std::string& token) {
  if (token.empty()) {
    return;
  }
  PrintSnapshot(machine->HandleKey(ParseKeyToken(token)));
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string shared_data_dir =
        GetEnvOrDefault("PREDICTABLE_PINYIN_SHARED_DATA_DIR", "/usr/share/rime-data");
    const std::string default_user_data_dir =
        GetEnvOrDefault("HOME", "") + "/.config/ibus/rime";
    const char* configured_user_data_env = std::getenv("PREDICTABLE_PINYIN_USER_DATA_DIR");
    std::filesystem::path user_data_dir = configured_user_data_env == nullptr
        ? std::filesystem::path(default_user_data_dir)
        : std::filesystem::path(configured_user_data_env);
    ScopedDirectoryCleanup cleanup;

    // CLI tests should not contend on the same userdb lock. When an explicit
    // user-data dir is provided, default to cloning it into a temp run dir.
    if (configured_user_data_env != nullptr &&
        !EnvFlagEnabled("PREDICTABLE_PINYIN_REUSE_USER_DATA_DIR")) {
      user_data_dir = CreateIsolatedUserDataDir(user_data_dir);
      cleanup = ScopedDirectoryCleanup(user_data_dir);
    }

    const std::filesystem::path prism_path(
        GetEnvOrDefault("PREDICTABLE_PINYIN_PRISM_PATH", "data/raw/pinyin_simp.prism.txt"));

    const std::filesystem::path stroke_dict_path(
        GetEnvOrDefault("PREDICTABLE_PINYIN_STROKE_DICT_PATH", "data/raw/stroke.dict.yaml"));
    const std::filesystem::path hanzi_db_path(
        GetEnvOrDefault("PREDICTABLE_PINYIN_HANZI_DB_PATH", "data/raw/hanzi_db.csv"));
    const std::filesystem::path pinyin_dict_path(
        GetEnvOrDefault("PREDICTABLE_PINYIN_PINYIN_DICT_PATH", "data/raw/pinyin_simp.dict.yaml"));

    RimeSession session(shared_data_dir, user_data_dir.string(), "predictable_pinyin");
    PredictableStateMachine machine(&session);
    if (!machine.Initialize(prism_path, stroke_dict_path, hanzi_db_path, pinyin_dict_path)) {
      throw std::runtime_error(
          "Failed to initialize: prism=" + prism_path.string() +
          " stroke_dict=" + stroke_dict_path.string() +
          " hanzi_db=" + hanzi_db_path.string());
    }

    if (argc > 1) {
      for (int i = 1; i < argc; ++i) {
        RunToken(&machine, argv[i]);
      }
      return 0;
    }

    std::cout << "Predictable Pinyin CLI\n";
    std::cout << "Enter one key token per line. Use <space> or <backspace>. Ctrl-D exits.\n\n";
    PrintSnapshot(machine.Snapshot());

    std::string token;
    while (std::getline(std::cin, token)) {
      RunToken(&machine, token);
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << '\n';
    return 1;
  }
}
