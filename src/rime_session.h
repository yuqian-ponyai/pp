#ifndef PREDICTABLE_PINYIN_RIME_SESSION_H_
#define PREDICTABLE_PINYIN_RIME_SESSION_H_

#include <cstddef>
#include <string>
#include <vector>

#include <rime_api.h>

namespace predictable_pinyin {

struct RimeContextSnapshot {
  std::string input;
  std::string preedit;
  std::vector<std::string> candidates;
};

class Session {
 public:
  virtual ~Session() = default;

  virtual bool ProcessAsciiKey(char key) = 0;
  virtual bool ProcessBackspace() = 0;
  virtual void ClearComposition() = 0;

  virtual std::string GetRawInput() const = 0;
  virtual std::vector<std::string> GetCandidates(std::size_t limit = 32) const = 0;
  virtual int GetCandidateCount(std::size_t limit = 256) const = 0;
  virtual std::string CommitSelectedCandidate(int index) = 0;
  virtual RimeContextSnapshot Snapshot(std::size_t limit = 8) const = 0;
};

class RimeSession : public Session {
 public:
  RimeSession(const std::string& shared_data_dir,
              const std::string& user_data_dir,
              const std::string& schema_id);
  ~RimeSession() override;

  RimeSession(const RimeSession&) = delete;
  RimeSession& operator=(const RimeSession&) = delete;

  bool ProcessAsciiKey(char key) override;
  bool ProcessBackspace() override;
  void ClearComposition() override;

  std::string GetRawInput() const override;
  std::vector<std::string> GetCandidates(std::size_t limit = 32) const override;
  int GetCandidateCount(std::size_t limit = 256) const override;
  std::string CommitSelectedCandidate(int index) override;
  bool ProbeHasCandidates(const std::string& input);
  RimeContextSnapshot Snapshot(std::size_t limit = 8) const override;

 private:
  bool ProcessKey(int keycode);
  bool RestoreInput(const std::string& input);
  bool SelectSchema(const std::string& schema_id);

  RimeApi* api_;
  RimeSessionId session_id_;
};

}  // namespace predictable_pinyin

#endif  // PREDICTABLE_PINYIN_RIME_SESSION_H_
