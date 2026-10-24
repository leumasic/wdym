#include <wdym/tokenizers/bpe.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
constexpr std::string_view whitespace = " \t\r\n";

struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};
}  // namespace

using wdym::BytePairEncoding;

BytePairEncoding::BytePairEncoding() {}

BytePairEncoding BytePairEncoding::train_from_texts(TextStream texts, const TrainOptions& options) {
  std::unordered_map<std::string, uint64_t, StringHash, std::equal_to<>> wordToCount;

  for (const auto text : texts) {
    size_t start = text.find_first_not_of(whitespace);

    while (start != std::string_view::npos) {
      const auto end = text.find_first_of(whitespace, start);
      const auto word = text.substr(start, end - start);

      if (auto it = wordToCount.find(word); it != wordToCount.end())
        ++it->second;
      else
        wordToCount.emplace(word, 1);

      start = text.find_first_not_of(whitespace, end);
    }
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
