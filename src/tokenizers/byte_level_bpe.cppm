export module wdym:byte_level_bpe;

import std;
import :normalization;
import :tokenizer;

export namespace wdym {
struct ByteLevelBPETrainOptions {
  int maxVocabularySize = 8000;
};

class ByteLevelBPETokenizer
    : public TokenizerBase<ByteLevelBPETokenizer, ByteLevelBPETrainOptions> {
  friend TokenizerBase;

 public:
  std::vector<std::byte> toBytes() const;
  static ByteLevelBPETokenizer fromBytes(std::span<const std::byte> bytes);

  std::vector<std::uint32_t> encode(std::string_view text) const;
  std::string decode(
      std::span<const std::uint32_t> tokenIds,
      const DecodeOptions& options = {}
  ) const;

  std::optional<std::uint32_t> tokenToId(std::string_view token) const;
  std::optional<std::uint32_t> markupTokenToId(std::string_view token) const;

  std::unordered_map<std::string, int> vocabulary;

 private:
  static ByteLevelBPETokenizer trainFromTexts(
      TextStream texts,
      const TrainOptions& options,
      const Settings& settings,
      Normalizer normalizer
  );

  explicit ByteLevelBPETokenizer(Normalizer normalizer);
};
}  // namespace wdym
