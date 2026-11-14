module wdym;

import std;

using wdym::ByteLevelBPETokenizer;

ByteLevelBPETokenizer::ByteLevelBPETokenizer(Normalizer normalizer)
    : TokenizerBase(std::move(normalizer)) {}

ByteLevelBPETokenizer ByteLevelBPETokenizer::trainFromTexts(
    TextStream texts,
    const TrainOptions& options,
    Normalizer normalizer
) {
  for (const std::string_view text : texts) {
    // TODO
  }
  return ByteLevelBPETokenizer(std::move(normalizer));
}

std::vector<std::byte> ByteLevelBPETokenizer::to_bytes() const {
  // TODO
  return {};
}

ByteLevelBPETokenizer ByteLevelBPETokenizer::from_bytes(std::span<const std::byte> bytes) {
  // TODO
  return ByteLevelBPETokenizer(Normalizer(NormalizationSettings{}));
}
