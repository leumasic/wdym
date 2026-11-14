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

std::vector<std::uint32_t> ByteLevelBPETokenizer::encode(std::string_view text) const {
  // TODO
  return {};
}

std::string ByteLevelBPETokenizer::decode(
    std::span<const std::uint32_t> tokenIds,
    const DecodeOptions& options
) const {
  // TODO
  return {};
}
