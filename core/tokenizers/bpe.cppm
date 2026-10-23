export module wdym:bpe;

import std.compat;
import :tokenizer;

export namespace wdym {
struct BytePairEncodingTrainOptions {
  int vocab_size = 8000;
};

class BytePairEncoding : public TokenizerBase<BytePairEncoding, BytePairEncodingTrainOptions> {
 public:
  static BytePairEncoding train_from_texts(TextStream texts, const TrainOptions& options);

  void save(const std::filesystem::path& path) const;

  void encode();
  void decode();

 private:
  explicit BytePairEncoding();

  std::vector<std::string> idToSymbol;
  std::vector<std::pair<uint32_t, uint32_t>> merges;
};
}  // namespace wdym

namespace wdym {
template <>
constexpr std::string_view tokenizer_name<BytePairEncoding> = "BytePairEncoding";
}  // namespace wdym
