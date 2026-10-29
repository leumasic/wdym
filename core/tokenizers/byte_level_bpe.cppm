export module wdym:byte_level_bpe;

import std;
import :tokenizer;

export namespace wdym {
struct ByteLevelBPETrainOptions {
  int vocab_size = 8000;
};

class ByteLevelBPETokenizer
    : public TokenizerBase<ByteLevelBPETokenizer, ByteLevelBPETrainOptions> {
 public:
  static ByteLevelBPETokenizer from_file(const std::filesystem::path& path);
  void save(const std::filesystem::path& path) const;

  void encode();
  void decode();

  std::unordered_map<std::string, int> vocabulary;

 private:
  friend TokenizerBase;

  static ByteLevelBPETokenizer train_from_texts(TextStream texts, const TrainOptions& options);

  explicit ByteLevelBPETokenizer();
};
}  // namespace wdym

namespace wdym {
template <>
constexpr std::string_view tokenizer_name<ByteLevelBPETokenizer> = "ByteLevelBPETokenizer";
}  // namespace wdym
