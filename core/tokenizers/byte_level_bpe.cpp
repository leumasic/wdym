module wdym;

import std;

using wdym::ByteLevelBPETokenizer;

ByteLevelBPETokenizer::ByteLevelBPETokenizer() {}

ByteLevelBPETokenizer ByteLevelBPETokenizer::train_from_texts(
    TextStream texts,
    const TrainOptions& options
) {
  for (const std::string_view text : texts) {
    // TODO
  }
  return ByteLevelBPETokenizer();
}

ByteLevelBPETokenizer ByteLevelBPETokenizer::from_file(const std::filesystem::path& path) {
  // TODO
  return ByteLevelBPETokenizer();
}

void ByteLevelBPETokenizer::save(const std::filesystem::path& path) const {
  // TODO
}
