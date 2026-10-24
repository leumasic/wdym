#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include "wdym/tokenizer.h"

namespace wdym {
struct BytePairEncodingTrainOptions {
  int vocab_size = 8000;
};

class BytePairEncoding : public TokenizerBase<BytePairEncoding, BytePairEncodingTrainOptions> {
 public:
  static BytePairEncoding train_from_texts(TextStream texts, const TrainOptions& options);

  static BytePairEncoding from_file(const std::filesystem::path& path);
  void save(const std::filesystem::path& path) const;

  void encode();
  void decode();

  std::unordered_map<std::string, int> vocabulary;

 private:
  explicit BytePairEncoding();
};

template <>
inline constexpr std::string_view tokenizer_name<BytePairEncoding> = "BytePairEncoding";
}  // namespace wdym
