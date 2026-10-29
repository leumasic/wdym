export module wdym:bpe;

import std.compat;
import :tokenizer;

export namespace wdym {
struct BytePairEncodingTrainOptions {
  int vocab_size = 8000;
};

class BytePairEncoding : public TokenizerBase<BytePairEncoding, BytePairEncodingTrainOptions> {
 public:
  void save(const std::filesystem::path& path) const;

  void encode();
  void decode();

  bool operator==(const BytePairEncoding& other) const {
    return idToSymbol_ == other.idToSymbol_ && merges_ == other.merges_;
  }

  auto merged_symbols() const {
    std::vector<std::pair<std::string, std::string>> result;

    result.reserve(merges_.size());
    for (const auto& [left, right] : merges_)
      result.emplace_back(idToSymbol_[left], idToSymbol_[right]);

    return result;
  }

 private:
  friend TokenizerBase;

  static BytePairEncoding train_from_texts(TextStream texts, const TrainOptions& options);

  explicit BytePairEncoding();

  std::vector<std::string> idToSymbol_;
  std::vector<std::pair<uint32_t, uint32_t>> merges_;
};
}  // namespace wdym

namespace wdym {
template <>
constexpr std::string_view tokenizer_name<BytePairEncoding> = "BytePairEncoding";
}  // namespace wdym
