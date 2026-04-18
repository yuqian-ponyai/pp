#include "rime_session.h"

#include <cstdlib>
#include <mutex>
#include <stdexcept>

namespace predictable_pinyin {

namespace {

constexpr int kBackspaceKey = 0xff08;
std::mutex g_rime_runtime_mutex;
int g_rime_runtime_users = 0;
bool g_rime_runtime_configured = false;

std::string ReadCString(const char* value) {
  return value == nullptr ? std::string() : std::string(value);
}

RimeTraits MakeTraits(const std::string& shared_data_dir,
                      const std::string& user_data_dir) {
  RimeTraits traits{};
  traits.data_size = sizeof(RimeTraits) - sizeof(traits.data_size);
  traits.shared_data_dir = shared_data_dir.c_str();
  traits.user_data_dir = user_data_dir.c_str();
  traits.distribution_name = "Predictable Pinyin";
  traits.distribution_code_name = "predictable-pinyin";
  traits.distribution_version = "0.1";
  traits.app_name = "rime.predictable-pinyin";
  return traits;
}

void AcquireRimeRuntime(RimeApi* api, const std::string& shared_data_dir,
                        const std::string& user_data_dir) {
  std::lock_guard<std::mutex> lock(g_rime_runtime_mutex);
  if (g_rime_runtime_users == 0) {
    RimeTraits traits = MakeTraits(shared_data_dir, user_data_dir);
    if (!g_rime_runtime_configured) {
      api->setup(&traits);
      g_rime_runtime_configured = true;
    }
    api->initialize(&traits);
  }
  ++g_rime_runtime_users;
}

void ReleaseRimeRuntime(RimeApi* api) {
  std::lock_guard<std::mutex> lock(g_rime_runtime_mutex);
  if (g_rime_runtime_users <= 0) {
    return;
  }
  --g_rime_runtime_users;
  if (g_rime_runtime_users == 0) {
    api->finalize();
  }
}

}  // namespace

RimeSession::RimeSession(const std::string& shared_data_dir,
                         const std::string& user_data_dir,
                         const std::string& schema_id)
    : api_(rime_get_api()), session_id_(0) {
  if (api_ == nullptr) {
    throw std::runtime_error("Failed to acquire Rime API.");
  }

  AcquireRimeRuntime(api_, shared_data_dir, user_data_dir);
  session_id_ = api_->create_session();
  if (session_id_ == 0) {
    ReleaseRimeRuntime(api_);
    throw std::runtime_error("Failed to create Rime session.");
  }
  if (!SelectSchema(schema_id)) {
    api_->destroy_session(session_id_);
    session_id_ = 0;
    ReleaseRimeRuntime(api_);
    throw std::runtime_error("Failed to select schema: " + schema_id);
  }
}

RimeSession::~RimeSession() {
  if (api_ != nullptr && session_id_ != 0) {
    api_->destroy_session(session_id_);
  }
  if (api_ != nullptr) {
    ReleaseRimeRuntime(api_);
  }
}

bool RimeSession::ProcessAsciiKey(char key) {
  return ProcessKey(static_cast<unsigned char>(key));
}

bool RimeSession::ProcessBackspace() {
  return ProcessKey(kBackspaceKey);
}

void RimeSession::ClearComposition() {
  api_->clear_composition(session_id_);
}

std::string RimeSession::GetRawInput() const {
  if (!RIME_API_AVAILABLE(api_, get_input)) {
    return {};
  }
  return ReadCString(api_->get_input(session_id_));
}

std::vector<std::string> RimeSession::GetCandidates(std::size_t limit) const {
  std::vector<std::string> results;
  if (!RIME_API_AVAILABLE(api_, candidate_list_begin)) {
    return results;
  }

  RimeCandidateListIterator iterator{};
  if (!api_->candidate_list_begin(session_id_, &iterator)) {
    return results;
  }

  while (api_->candidate_list_next(&iterator)) {
    results.push_back(ReadCString(iterator.candidate.text));
    if (results.size() >= limit) {
      break;
    }
  }

  api_->candidate_list_end(&iterator);
  return results;
}

int RimeSession::GetCandidateCount(std::size_t limit) const {
  return static_cast<int>(GetCandidates(limit).size());
}

std::string RimeSession::CommitSelectedCandidate(int index) {
  if (!RIME_API_AVAILABLE(api_, select_candidate) || index < 0) {
    return {};
  }
  if (!api_->select_candidate(session_id_, static_cast<std::size_t>(index))) {
    return {};
  }
  api_->commit_composition(session_id_);

  RimeCommit commit{};
  commit.data_size = sizeof(RimeCommit) - sizeof(commit.data_size);
  if (!api_->get_commit(session_id_, &commit)) {
    return {};
  }
  const std::string committed = ReadCString(commit.text);
  api_->free_commit(&commit);
  return committed;
}

bool RimeSession::ProbeHasCandidates(const std::string& input) {
  const std::string previous = GetRawInput();
  ClearComposition();
  if (!RestoreInput(input)) {
    ClearComposition();
    RestoreInput(previous);
    return false;
  }
  const bool has_candidates = GetCandidateCount(16) > 0;
  ClearComposition();
  RestoreInput(previous);
  return has_candidates;
}

RimeContextSnapshot RimeSession::Snapshot(std::size_t limit) const {
  RimeContextSnapshot snapshot;
  snapshot.input = GetRawInput();
  snapshot.candidates = GetCandidates(limit);

  RimeContext context{};
  context.data_size = sizeof(RimeContext) - sizeof(context.data_size);
  if (api_->get_context(session_id_, &context)) {
    snapshot.preedit = ReadCString(context.composition.preedit);
    api_->free_context(&context);
  }

  return snapshot;
}

bool RimeSession::ProcessKey(int keycode) {
  return api_->process_key(session_id_, keycode, 0);
}

bool RimeSession::RestoreInput(const std::string& input) {
  for (char c : input) {
    if (!ProcessAsciiKey(c)) {
      return false;
    }
  }
  return true;
}

bool RimeSession::SelectSchema(const std::string& schema_id) {
  return api_->select_schema(session_id_, schema_id.c_str());
}

}  // namespace predictable_pinyin
