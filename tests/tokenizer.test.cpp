#include <catch2/catch_test_macros.hpp>

import std;
import wdym;

using namespace wdym::literals;
using wdym::BytePairEncoding;
using wdym::BytePairEncodingSettings;

const std::vector<std::string> TEXTS = {
    "the quick brown fox jumps over the lazy dog",
    "the lazy dog sleeps",
    "a quick brown dog",
};

const BytePairEncodingSettings BPE_SETTINGS{
    .wordPrefix = "",
    .wordSuffix = "</w>",
    .adjoinSuffix = false,
};

TEST_CASE("text order does not change tokenizer merges", "[bpe]") {
  auto firstSequencePart = {"low", "low"};
  auto secondSequencePart = {"lower", "newest", "newest"};

  auto mergedSequences = std::views::concat(firstSequencePart, secondSequencePart);
  auto mergedWithReversedSecond =
      std::views::concat(firstSequencePart, secondSequencePart | std::views::reverse);

  auto tokenizer = BytePairEncoding::train(mergedSequences);
  auto reversedTokenizer = BytePairEncoding::train(mergedWithReversedSecond);

  REQUIRE(tokenizer == reversedTokenizer);
}

TEST_CASE("training produces tokenizer", "[bpe]") {
  // example from Speech And Language Processing (Dan & Jurafsky)
  auto sequence = {"set", "new", "new", "renew", "reset", "renew"};

  const std::vector<std::pair<std::string, std::string>> expected{
      {"e", "w"},
      {"n", "ew"},
      {"new", "</w>"},
      {"r", "e"},
      {"e", "t"},
      {"s", "et"},
      {"re", "new</w>"},
      {"set", "</w>"},
      {"re", "set</w>"},
  };

  REQUIRE(BytePairEncoding::train(sequence, {}, BPE_SETTINGS).merges() == expected);
}

TEST_CASE("vocab size limits the number of merges", "[bpe]") {
  auto sequence = {"set", "new", "new", "renew", "reset", "renew"};

  REQUIRE(
      BytePairEncoding::train(sequence, {.maxVocabularySize = 11}, BPE_SETTINGS).merges().size() ==
      4
  );
}

TEST_CASE("decoding encoded text gives back the text", "[bpe]") {
  const auto tokenizer = BytePairEncoding::train(TEXTS);

  for (const std::string& text : TEXTS)
    REQUIRE(tokenizer.decode(tokenizer.encode(text)) == text);
}

TEST_CASE("saved tokenizers load back unchanged", "[bpe]") {
  const auto tokenizer = BytePairEncoding::train(TEXTS);

  const auto loadedFromBytes = BytePairEncoding::fromBytes(tokenizer.toBytes());
  REQUIRE(loadedFromBytes == tokenizer);

  const auto path = std::filesystem::temp_directory_path() / "wdym-test-tokenizer.wdym";
  tokenizer.save(path);
  const auto loadedFromFile = BytePairEncoding::load(path);
  std::filesystem::remove(path);

  REQUIRE(loadedFromFile == tokenizer);
  REQUIRE(loadedFromFile.encode(TEXTS[0]) == tokenizer.encode(TEXTS[0]));
}

TEST_CASE("markup tokens encode and decode as single tokens", "[bpe]") {
  const auto tokenizer = BytePairEncoding::train(TEXTS, {}, {.markupTokens = {"<s>", "</s>"}});

  const std::optional<std::uint32_t> startTokenId = tokenizer.markupTokenToId("<s>");
  REQUIRE(startTokenId.has_value());
  REQUIRE_FALSE(tokenizer.markupTokenToId("<unknown>").has_value());

  const std::vector<wdym::TextOrMarkupToken> parts = {"<s>"_markup, "the lazy dog", "</s>"_markup};
  const std::vector<std::uint32_t> tokenIds = tokenizer.encodeParts(parts);

  REQUIRE(tokenIds.front() == *startTokenId);
  REQUIRE(tokenIds.back() == tokenizer.markupTokenToId("</s>"));
  const std::vector<wdym::TextOrMarkupToken> expectedParts = {
      "<s>"_markup, "the lazy dog", "</s>"_markup
  };
  REQUIRE(tokenizer.decodeParts(tokenIds) == expectedParts);
}

TEST_CASE("byte fallback encodes characters unseen in training", "[bpe]") {
  const auto tokenizer =
      BytePairEncoding::train(TEXTS, {}, {.unknownCharacterFallback = wdym::ByteFallback{}});

  REQUIRE(tokenizer.decode(tokenizer.encode("the dog Ω")) == "the dog Ω");
}

TEST_CASE("normalization applies replacement rules", "[normalization]") {
  const wdym::NormalizationSettings normalization{
      .steps = {{.name = "lowercase", .rules = {{.input = "T", .output = "t"}}}},
  };
  const auto tokenizer = BytePairEncoding::train(TEXTS, {}, {}, normalization);

  REQUIRE(tokenizer.normalize("The dog") == "the dog");
  REQUIRE(tokenizer.encode("The dog") == tokenizer.encode("the dog"));
}

TEST_CASE("invalid input is rejected", "[bpe]") {
  const std::vector<std::byte> notATokenizerFile(64, std::byte{1});
  REQUIRE_THROWS_AS(BytePairEncoding::fromBytes(notATokenizerFile), std::invalid_argument);

  const BytePairEncodingSettings unlistedMarkupFallback{
      .unknownCharacterFallback = wdym::MarkupToken{"<unk>"},
  };
  REQUIRE_THROWS_AS(
      BytePairEncoding::train(TEXTS, {}, unlistedMarkupFallback), std::invalid_argument
  );
}
