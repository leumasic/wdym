import std;
import wdym;

using wdym::BytePairEncoding;

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error(std::format("could not open {}", path.string()));

  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);)
    lines.push_back(std::move(line));

  return lines;
}

std::vector<std::uint32_t> parseTokenIds(std::string_view line) {
  std::vector<std::uint32_t> tokenIds;
  for (const auto tokenId : std::views::split(line, ' '))
    if (!tokenId.empty())
      tokenIds.push_back(
          static_cast<std::uint32_t>(std::stoul(std::string(std::string_view(tokenId))))
      );

  return tokenIds;
}

std::string formatTokenIds(std::span<const std::uint32_t> tokenIds) {
  return tokenIds |
         std::views::transform([](std::uint32_t tokenId) { return std::to_string(tokenId); }) |
         std::views::join_with(' ') |
         std::ranges::to<std::string>();
}

int main(int argc, char** argv) {
  if (argc != 5) {
    std::println(
        std::cerr,
        "usage: {} <tokenizer.model> <text lines> <sentencepiece token ids> <sentencepiece decoded "
        "lines>",
        argv[0]
    );
    return 2;
  }

  const auto tokenizer = BytePairEncoding::loadSentencePiece(argv[1]);
  const auto texts = readLines(argv[2]);
  const auto sentencePieceTokenIds = readLines(argv[3]);
  const auto sentencePieceDecodedTexts = readLines(argv[4]);

  if (sentencePieceTokenIds.size() != texts.size() ||
      sentencePieceDecodedTexts.size() != texts.size()) {
    std::println(std::cerr, "the text, token ID and decoded files have different line counts");
    return 2;
  }

  constexpr std::size_t MAX_PRINTED_MISMATCHES = 5;
  std::size_t encodeMismatchCount = 0;
  std::size_t decodeMismatchCount = 0;
  std::size_t tokenCount = 0;
  BytePairEncoding::WordCache cache;

  for (std::size_t lineIndex = 0; lineIndex < texts.size(); ++lineIndex) {
    std::vector<std::uint32_t> tokenIds;
    tokenizer.encode(texts[lineIndex], tokenIds, cache);
    tokenCount += tokenIds.size();

    if (const auto expectedTokenIds = parseTokenIds(sentencePieceTokenIds[lineIndex]);
        tokenIds != expectedTokenIds && encodeMismatchCount++ < MAX_PRINTED_MISMATCHES)
      std::println(
          "encode mismatch on line {}: {:?}\n  sentencepiece {}\n  wdym          {}",
          lineIndex + 1,
          texts[lineIndex],
          sentencePieceTokenIds[lineIndex],
          formatTokenIds(tokenIds)
      );

    if (const auto decodedText = tokenizer.decode(parseTokenIds(sentencePieceTokenIds[lineIndex]));
        decodedText != sentencePieceDecodedTexts[lineIndex] &&
        decodeMismatchCount++ < MAX_PRINTED_MISMATCHES)
      std::println(
          "decode mismatch on line {}:\n  sentencepiece {:?}\n  wdym          {:?}",
          lineIndex + 1,
          sentencePieceDecodedTexts[lineIndex],
          decodedText
      );
  }

  if (!(BytePairEncoding::fromBytes(tokenizer.toBytes()) == tokenizer)) {
    std::println("saving and loading the converted tokenizer changes it");
    return 1;
  }

  std::println(
      "{} lines, {} tokens: {} encode mismatches, {} decode mismatches",
      texts.size(),
      tokenCount,
      encodeMismatchCount,
      decodeMismatchCount
  );

  return encodeMismatchCount == 0 && decodeMismatchCount == 0 ? 0 : 1;
}
