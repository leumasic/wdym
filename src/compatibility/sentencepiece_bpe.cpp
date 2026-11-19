module;

#include <ankerl/unordered_dense.h>

module wdym;

import std.compat;
import :containers;
import :sentencepiece_model;
import :utf8;

using wdym::BytePairEncoding;
using wdym::SentencePieceModel;
using wdym::SentencePieceType;
using wdym::StringKeyMap;
using wdym::StringKeySet;
namespace unordered_dense = ankerl::unordered_dense;

namespace {
constexpr std::string_view SENTENCEPIECE_WHITESPACE = "▁";

[[noreturn]] void throwUnsupported(std::string_view feature) {
  throw std::invalid_argument(
      std::format("SentencePiece models with {} aren't supported yet", feature)
  );
}

[[noreturn]] void throwCorrupted(std::string_view problem) {
  throw std::invalid_argument(std::format("the SentencePiece model {}", problem));
}

void assertIsSupported(const SentencePieceModel& model) {
  if (model.algorithm != wdym::SentencePieceAlgorithm::BytePairEncoding)
    throw std::invalid_argument("the SentencePiece model isn't a BPE model");

  if (model.treatWhitespaceAsSuffix)
    throwUnsupported("treat_whitespace_as_suffix");

  if (!model.escapeWhitespaces)
    throwUnsupported("escape_whitespaces disabled");

  if (!model.addDummyPrefix)
    throwUnsupported("add_dummy_prefix disabled");

  if (!model.precompiledCharacterMap.empty())
    throwUnsupported(std::format("the {:?} normalizer", model.normalizerName));
}

std::optional<uint8_t> parseBytePiece(std::string_view piece) {
  if (piece.size() != 6 || !piece.starts_with("<0x") || !piece.ends_with('>'))
    return std::nullopt;

  uint8_t byte = 0;
  const char* const hexEnd = piece.data() + 5;
  if (const auto [end, error] = std::from_chars(piece.data() + 3, hexEnd, byte, 16);
      error != std::errc() || end != hexEnd)
    return std::nullopt;

  return byte;
}

size_t getCharacterSize(std::string_view text) {
  return std::max(1, std::countl_one(static_cast<unsigned char>(text.front())));
}

uint64_t getPairKey(uint32_t leftSymbolId, uint32_t rightSymbolId) {
  return (uint64_t{leftSymbolId} << 32) | rightSymbolId;
}

struct NormalPiece {
  uint32_t symbolId;
  uint32_t tokenId;
  float score;
};

unordered_dense::map<uint64_t, uint32_t> buildPairKeyToMergedSymbolId(
    const StringKeySet& symbols,
    std::vector<NormalPiece> normalPieces
) {
  const auto findNormalPieceSymbolId = [&](std::string_view piece) -> std::optional<uint32_t> {
    const auto it = symbols.find(piece);
    if (it == symbols.end() || std::cmp_greater_equal(it - symbols.begin(), normalPieces.size()))
      return std::nullopt;

    return static_cast<uint32_t>(it - symbols.begin());
  };

  std::ranges::sort(normalPieces, [](const NormalPiece& left, const NormalPiece& right) {
    return left.score != right.score ? left.score > right.score : left.tokenId < right.tokenId;
  });

  unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId;

  for (const NormalPiece& normalPiece : normalPieces) {
    const std::string_view piece = symbols.values()[normalPiece.symbolId];

    for (
        size_t splitPosition = getCharacterSize(piece); splitPosition < piece.size();
        splitPosition += getCharacterSize(piece.substr(splitPosition))
    ) {
      const auto leftSymbolId = findNormalPieceSymbolId(piece.substr(0, splitPosition));
      const auto rightSymbolId = findNormalPieceSymbolId(piece.substr(splitPosition));

      if (leftSymbolId.has_value() && rightSymbolId.has_value())
        pairKeyToMergedSymbolId.emplace(
            getPairKey(*leftSymbolId, *rightSymbolId), normalPiece.symbolId
        );
    }
  }

  return pairKeyToMergedSymbolId;
}
}  // namespace

BytePairEncoding BytePairEncoding::fromSentencePiece(std::span<const std::byte> bytes) {
  const SentencePieceModel model = readSentencePieceModel(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
  );
  assertIsSupported(model);

  if (std::cmp_greater(model.pieces.size(), std::numeric_limits<uint32_t>::max()))
    throwCorrupted("has more pieces than 32-bit token IDs allow");

  StringKeySet symbols;
  std::vector<uint32_t> symbolIdToTokenId;
  const auto addSymbol = [&](std::string_view symbol, uint32_t tokenId) {
    if (!symbols.emplace(symbol).second)
      throwCorrupted(std::format("has the piece {:?} twice", symbol));

    symbolIdToTokenId.push_back(tokenId);
  };

  StringKeyMap<uint32_t> markupTokenToTokenId;
  std::vector<std::string> markupTokens;
  const auto addMarkupToken = [&](std::string_view markupToken, uint32_t tokenId) {
    if (!markupTokenToTokenId.emplace(markupToken, tokenId).second)
      throwCorrupted(std::format("has the piece {:?} twice", markupToken));

    markupTokens.emplace_back(markupToken);
  };

  std::vector<NormalPiece> normalPieces;
  std::optional<std::string_view> unknownPiece;
  std::array<std::optional<uint32_t>, 256> byteToTokenId;

  for (uint32_t tokenId = 0; tokenId < model.pieces.size(); ++tokenId) {
    const auto& [piece, score, type] = model.pieces[tokenId];

    if (!isValidUtf8(piece))
      throwCorrupted(std::format("has the piece {:?}, which is not valid UTF-8", piece));

    switch (type) {
      case SentencePieceType::Normal:
        if (std::isnan(score))
          throwCorrupted(std::format("gives the piece {:?} a score that isn't a number", piece));

        normalPieces.push_back(
            {.symbolId = static_cast<uint32_t>(symbols.size()), .tokenId = tokenId, .score = score}
        );
        addSymbol(piece, tokenId);
        break;

      case SentencePieceType::Unknown:
        if (unknownPiece.has_value())
          throwCorrupted("has more than one unknown piece");

        unknownPiece = piece;
        addMarkupToken(piece, tokenId);
        break;

      case SentencePieceType::Control:
        addMarkupToken(piece, tokenId);
        break;

      case SentencePieceType::Byte: {
        const auto byte = parseBytePiece(piece);
        if (!byte.has_value())
          throwCorrupted(std::format("has the byte piece {:?}, which doesn't name a byte", piece));

        if (byteToTokenId[*byte].has_value())
          throwCorrupted(std::format("has the byte piece {:?} twice", piece));

        byteToTokenId[*byte] = tokenId;
        break;
      }

      case SentencePieceType::UserDefined:
        throwUnsupported("user-defined pieces");

      case SentencePieceType::Unused:
        throwUnsupported("unused pieces");
    }
  }

  if (!unknownPiece.has_value())
    throwCorrupted("has no unknown piece");

  auto pairKeyToMergedSymbolId = buildPairKeyToMergedSymbolId(symbols, std::move(normalPieces));

  std::vector<DecodeOnlyToken> decodeOnlyTokens;
  for (int byte = 0; byte < 256; ++byte) {
    if (!byteToTokenId[byte].has_value())
      continue;

    const std::string symbol(1, static_cast<char>(byte));
    if (const auto it = symbols.find(symbol); it != symbols.end())
      decodeOnlyTokens.push_back(
          {.tokenId = *byteToTokenId[byte], .symbolId = static_cast<uint32_t>(it - symbols.begin())}
      );
    else
      addSymbol(symbol, *byteToTokenId[byte]);
  }

  if (model.byteFallback)
    for (int byte = 0; byte < 256; ++byte)
      if (!symbols.contains(std::string(1, static_cast<char>(byte))))
        throwCorrupted("uses byte fallback without all 256 byte pieces");

  std::ranges::sort(markupTokens);

  BytePairEncodingSettings settings{
      .markupTokens = std::move(markupTokens),
      .wordDelimiter = " ",
      .extraWordDelimiters = model.removeExtraWhitespaces ? ExtraWordDelimiters::Drop
                                                          : ExtraWordDelimiters::PrefixNextWord,
      .wordPrefix = std::string(SENTENCEPIECE_WHITESPACE),
      .wordSuffix = "",
      .adjoinPrefix = false,
  };

  if (model.byteFallback)
    settings.unknownCharacterFallback = ByteFallback{};
  else
    settings.unknownCharacterFallback = MarkupToken{std::string(*unknownPiece)};

  return BytePairEncoding(
      Normalizer(
          NormalizationSettings{
              .steps = {{
                  .name = "SentencePiece whitespace",
                  .rules = {{.input = std::string(SENTENCEPIECE_WHITESPACE), .output = " "}},
              }},
          }
      ),
      std::move(settings),
      std::move(symbols),
      std::move(pairKeyToMergedSymbolId),
      std::move(markupTokenToTokenId),
      std::move(symbolIdToTokenId),
      std::move(decodeOnlyTokens)
  );
}

BytePairEncoding BytePairEncoding::loadSentencePiece(const std::filesystem::path& path) {
  return fromSentencePiece(readTokenizerFile(path));
}
