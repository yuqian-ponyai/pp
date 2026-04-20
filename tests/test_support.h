#ifndef PREDICTABLE_PINYIN_TEST_SUPPORT_H_
#define PREDICTABLE_PINYIN_TEST_SUPPORT_H_

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "predictable_state_machine.h"

namespace predictable_pinyin::test {

// Minimal Session implementation for unit tests. Candidates are keyed by the
// accumulated raw ASCII input so tests can specify exact per-input responses.
class FakeSession : public Session {
 public:
  explicit FakeSession(std::map<std::string, std::vector<std::string>> candidates_by_input)
      : candidates_by_input_(std::move(candidates_by_input)) {}

  bool ProcessAsciiKey(char key) override {
    raw_input_.push_back(key);
    RefreshCandidates();
    return true;
  }

  bool ProcessBackspace() override {
    if (!raw_input_.empty()) raw_input_.pop_back();
    RefreshCandidates();
    return true;
  }

  void ClearComposition() override {
    raw_input_.clear();
    RefreshCandidates();
  }

  std::string GetRawInput() const override { return raw_input_; }

  std::vector<std::string> GetCandidates(std::size_t limit = 32) const override {
    std::vector<std::string> result = candidates_;
    if (result.size() > limit) result.resize(limit);
    return result;
  }

  int GetCandidateCount(std::size_t limit = 256) const override {
    return static_cast<int>(GetCandidates(limit).size());
  }

  std::string CommitSelectedCandidate(int index) override {
    if (index < 0 || static_cast<std::size_t>(index) >= candidates_.size()) return {};
    last_commit_ = candidates_[index];
    ClearComposition();
    return last_commit_;
  }

  RimeContextSnapshot Snapshot(std::size_t limit = 8) const override {
    RimeContextSnapshot snapshot;
    snapshot.input = raw_input_;
    snapshot.preedit = raw_input_;
    snapshot.candidates = GetCandidates(limit);
    return snapshot;
  }

  const std::string& last_commit() const { return last_commit_; }

 private:
  void RefreshCandidates() {
    const auto it = candidates_by_input_.find(raw_input_);
    candidates_ = (it == candidates_by_input_.end()) ? std::vector<std::string>{} : it->second;
  }

  std::map<std::string, std::vector<std::string>> candidates_by_input_;
  std::string raw_input_;
  std::vector<std::string> candidates_;
  std::string last_commit_;
};

class ScopedDirectoryCleanup {
 public:
  explicit ScopedDirectoryCleanup(std::filesystem::path path)
      : path_(std::move(path)) {}

  ScopedDirectoryCleanup(const ScopedDirectoryCleanup&) = delete;
  ScopedDirectoryCleanup& operator=(const ScopedDirectoryCleanup&) = delete;

  ~ScopedDirectoryCleanup() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

 private:
  std::filesystem::path path_;
};

inline std::filesystem::path WriteSamplePrism() {
  const auto timestamp = std::to_string(
      static_cast<long long>(
          std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("predictable_pinyin_tests_" + timestamp);
  std::filesystem::create_directories(dir);

  std::ofstream out(dir / "sample.prism.txt");
  out << "ni\tni\t-\t0\n";
  out << "nin\tnin\t-\t0\n";
  out << "niao\tniao\t-\t0\n";
  out << "zhong\tzhong\t-\t0\n";
  out << "zho\tzhong\t-\t0\n";
  out << "agn\tang\t-\t0\n";
  out << "chang\tchang\t-\t0\n";
  out << "zhang\tzhang\t-\t0\n";
  out << "a\ta\t-\t0\n";
  out << "ai\tai\t-\t0\n";
  out << "an\tan\t-\t0\n";
  out << "ang\tang\t-\t0\n";
  out << "ao\tao\t-\t0\n";
  out << "ci\tci\t-\t0\n";
  out << "ca\tca\t-\t0\n";
  out << "cai\tcai\t-\t0\n";
  out << "guo\tguo\t-\t0\n";
  out << "gu\tgu\t-\t0\n";
  out << "yu\tyu\t-\t0\n";
  out << "qian\tqian\t-\t0\n";
  out << "sui\tsui\t-\t0\n";
  out << "shi\tshi\t-\t0\n";
  out << "shan\tshan\t-\t0\n";
  out << "de\tde\t-\t0\n";
  out << "di\tdi\t-\t0\n";
  out << "chen\tchen\t-\t0\n";
  out << "shen\tshen\t-\t0\n";
  out << "chong\tchong\t-\t0\n";
  out << "nu\tnu\t-\t0\n";
  out.close();

  return dir / "sample.prism.txt";
}

// Writes a minimal stroke.dict.yaml with a few characters used by tests.
// Returns the path to the file (inside the same temp dir as WriteSamplePrism
// if that was called first, or a fresh temp dir).
inline std::filesystem::path WriteSampleStrokeDict(
    const std::filesystem::path& dir) {
  std::ofstream out(dir / "sample.stroke.dict.yaml");
  // Minimal YAML header followed by data section.
  out << "---\nname: stroke\nversion: \"1.0\"\n...\n";
  // Actual stroke sequences from the upstream stroke.dict.yaml:
  //   中 szhs   种 phspnszhs   重 phszhhhsh
  //   你 pspzzpn  尼 zhphz
  out << "中\tszhs\n";
  out << "种\tphspnszhs\n";
  out << "重\tphszhhhsh\n";
  out << "你\tpspzzpn\n";
  out << "尼\tzhphz\n";
  out << "长\tphzn\n";
  out << "拟\thzpnz\n";
  out << "逆\tphzhn\n";
  out << "泥\tpspzhn\n";
  out << "鸟\tphzzn\n";
  out << "尿\thpzpnpn\n";
  out << "才\thsn\n";
  out << "从\tpnph\n";
  out << "国\tszzshh\n";
  out << "宇\tnnzhs\n";
  out << "骞\tnnzhhsshpnzzh\n";
  out << "岁\tszpnsh\n";
  out << "随\tzhpnsznh\n";
  out << "碎\thpshsph\n";
  out << "十\ths\n";
  out << "事\thszhzhhz\n";
  out << "千\tphs\n";
  out << "山\tszs\n";
  out << "奥\tpsznphspnhpn\n";
  out << "的\tpszhhpzn\n";
  out << "沈\tnnnnzpz\n";
  out << "努\tzphznzp\n";
  out << "怒\tzphznnznn\n";
  out.close();
  return dir / "sample.stroke.dict.yaml";
}

// Writes a minimal hanzi_db.csv with frequency ranks for characters used by
// tests. Returns the path to the file.
inline std::filesystem::path WriteSampleHanziDb(
    const std::filesystem::path& dir) {
  std::ofstream out(dir / "sample_hanzi_db.csv");
  out << "frequency_rank,character,pinyin,definition,radical,radical_code,"
         "stroke_count,hsk_level,general_standard_num\n";
  // Ranks chosen to verify stable sort and unknown-char ordering:
  //   在(6) < 中(54) < 种(736) < 你(73) < 尼(1185) < 重(233)
  out << "6,在,zài,be at,土,32.3,6,1,0388\n";
  out << "54,中,zhōng,central,丨,2.3,4,1,0081\n";
  out << "73,你,nǐ,you,人,9.5,7,1,0786\n";
  out << "233,重,zhòng,heavy,里,166.2,9,2,1811\n";
  out << "736,种,zhǒng,seed,禾,115.4,9,2,1807\n";
  out << "245,长,zhǎng,long,长,168.0,4,1,0125\n";
  out << "1185,尼,ní,Buddhist nun,尸,44.2,5,0,0229\n";
  out << "1800,拟,nǐ,to plan,手,64.3,7,0,0800\n";
  out << "1900,逆,nì,contrary,辶,162.6,9,0,1234\n";
  out << "1950,泥,ní,mud,水,85.5,8,0,0901\n";
  out << "2000,鸟,niǎo,bird,鸟,196.0,5,2,0248\n";
  out << "3000,尿,niào,urine,尸,44.4,7,0,0714\n";
  out << "200,才,cái,talent,才,64.0,3,1,0043\n";
  out << "500,从,cóng,from,人,9.3,4,1,0050\n";
  out << "150,国,guó,country,囗,31.0,8,1,0900\n";
  out << "800,宇,yǔ,house,宀,40.3,6,1,0500\n";
  out << "4000,骞,qiān,fly,马,187.4,13,0,5500\n";
  out << "300,岁,suì,year,山,46.3,6,1,0350\n";
  out << "400,随,suí,follow,阝,170.6,11,1,2300\n";
  out << "1500,碎,suì,broken,石,112.8,13,0,2800\n";
  out << "120,十,shí,ten,十,24.0,2,1,0015\n";
  out << "60,事,shì,affair,亅,6.0,8,1,0600\n";
  out << "350,千,qiān,thousand,十,24.1,3,1,0030\n";
  out << "400,山,shān,mountain,山,46.0,3,1,0040\n";
  out << "1200,奥,ào,mysterious,大,37.9,12,2,2300\n";
  out << "1,的,de,possessive,白,106.3,8,1,1155\n";
  out << "1681,沈,chén,sink,水,85.4,7,,0870\n";
  out << "1081,努,nǔ,exert,力,19.5,7,3,0924\n";
  out << "1143,怒,nù,anger,心,61.5,9,6,1742\n";
  out.close();
  return dir / "sample_hanzi_db.csv";
}

// Writes a minimal pinyin_simp.dict.yaml with multi-reading entries.
inline std::filesystem::path WriteSamplePinyinDict(
    const std::filesystem::path& dir) {
  std::ofstream out(dir / "sample.pinyin_simp.dict.yaml");
  out << "---\nname: pinyin_simp\nversion: \"0.1\"\nsort: by_weight\n...\n";
  // Multi-reading characters:
  out << "的\tde\t4828294\n";
  out << "的\tdi\t1589\n";
  out << "沈\tchen\t1\n";
  out << "沈\tshen\t6710\n";
  // 重 has two readings: zhong and chong
  out << "重\tzhong\t100000\n";
  out << "重\tchong\t5000\n";
  out.close();
  return dir / "sample.pinyin_simp.dict.yaml";
}

}  // namespace predictable_pinyin::test

#endif  // PREDICTABLE_PINYIN_TEST_SUPPORT_H_
