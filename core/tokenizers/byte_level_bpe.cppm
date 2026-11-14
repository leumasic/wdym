export module wdym:byte_level_bpe;

import std;
import :normalization;
import :tokenizer;

export namespace wdym {
struct ByteLevelBPETrainOptions {
  int max_vocabulary_size = 8000;
};

class ByteLevelBPETokenizer
    : public TokenizerBase<ByteLevelBPETokenizer, ByteLevelBPETrainOptions> {
  friend TokenizerBase;

 public:
  std::vector<std::byte> to_bytes() const;
  static ByteLevelBPETokenizer from_bytes(std::span<const std::byte> bytes);

  void encode();
  void decode();

  std::unordered_map<std::string, int> vocabulary;

 private:
  static ByteLevelBPETokenizer trainFromTexts(
      TextStream texts,
      const TrainOptions& options,
      Normalizer normalizer
  );

  explicit ByteLevelBPETokenizer(Normalizer normalizer);
};
}  // namespace wdym

namespace wdym {
template <>
constexpr std::string_view tokenizer_name<ByteLevelBPETokenizer> = "ByteLevelBPETokenizer";
}  // namespace wdym
