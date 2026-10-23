#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include "wdym/tokenizer.h"

namespace wdym {
struct ByteLevelBPETrainOptions {
  int vocab_size = 8000;
};

class ByteLevelBPETokenizer
    : public TokenizerBase<ByteLevelBPETokenizer, ByteLevelBPETrainOptions> {
 public:
  static ByteLevelBPETokenizer train_from_texts(TextStream texts, const TrainOptions& options);

  static ByteLevelBPETokenizer from_file(const std::filesystem::path& path);
  void save(const std::filesystem::path& path) const;

  // word -> id
  void encode();
  // id -> word
  void decode();

  std::unordered_map<std::string, int> vocabulary;

 private:
  explicit ByteLevelBPETokenizer();
};

template <>
inline constexpr std::string_view tokenizer_name<ByteLevelBPETokenizer> = "ByteLevelBPETokenizer";
}  // namespace wdym
