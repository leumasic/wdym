module wdym;

import std;

using wdym::ByteLevelBPETokenizer;

ByteLevelBPETokenizer::ByteLevelBPETokenizer(Normalizer normalizer)
    : TokenizerBase(std::move(normalizer)) {}

ByteLevelBPETokenizer ByteLevelBPETokenizer::trainFromTexts(
    TextStream texts,
    const TrainOptions& options,
    const Settings& settings,
    Normalizer normalizer
) {
  for (const std::string_view text : texts) {
    // TODO
  }
  return ByteLevelBPETokenizer(std::move(normalizer));
}

std::vector<std::byte> ByteLevelBPETokenizer::toBytes() const {
  // TODO
  return {};
}

ByteLevelBPETokenizer ByteLevelBPETokenizer::fromBytes(std::span<const std::byte> bytes) {
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

std::optional<std::uint32_t> ByteLevelBPETokenizer::tokenToId(std::string_view token) const {
  // TODO
  return std::nullopt;
}

std::optional<std::uint32_t> ByteLevelBPETokenizer::markupTokenToId(std::string_view token) const {
  // TODO
  return std::nullopt;
}
