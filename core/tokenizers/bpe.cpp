#include <wdym/tokenizers/bpe.h>

using wdym::BytePairEncoding;

BytePairEncoding::BytePairEncoding() {}

BytePairEncoding BytePairEncoding::train_from_texts(TextStream texts, const TrainOptions& options) {
  for (const std::string_view text : texts) {
    // TODO
  }
  return BytePairEncoding();
}

BytePairEncoding BytePairEncoding::from_file(const std::filesystem::path& path) {
  // TODO
  return BytePairEncoding();
}

void BytePairEncoding::save(const std::filesystem::path& path) const {
  // TODO
}
