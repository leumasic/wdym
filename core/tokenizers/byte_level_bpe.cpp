module wdym;

import std;

using wdym::ByteLevelBPETokenizer;

ByteLevelBPETokenizer::ByteLevelBPETokenizer(Normalizer normalizer)
    : TokenizerBase(std::move(normalizer)) {}

ByteLevelBPETokenizer ByteLevelBPETokenizer::train_from_texts(
    TextStream texts,
    const TrainOptions& options,
    Normalizer normalizer
) {
  for (const std::string_view text : texts) {
    // TODO
  }
  return ByteLevelBPETokenizer(std::move(normalizer));
}

ByteLevelBPETokenizer ByteLevelBPETokenizer::from_file(const std::filesystem::path& path) {
  // TODO
  return ByteLevelBPETokenizer(Normalizer(NormalizationSettings{}));
}

void ByteLevelBPETokenizer::save(const std::filesystem::path& path) const {
  // TODO
}
