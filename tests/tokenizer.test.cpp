#include <catch2/catch_test_macros.hpp>

import std;
import wdym;

using wdym::BytePairEncoding;

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
  auto tokenizer = BytePairEncoding::train(sequence);

  const std::vector<std::pair<std::string, std::string>> expected{
      {"e", "w"},  {"n", "ew"},       {"new", "</w>"}, {"r", "e"},        {"e", "t"},
      {"s", "et"}, {"re", "new</w>"}, {"set", "</w>"}, {"re", "set</w>"},
  };

  REQUIRE(BytePairEncoding::train(sequence).merged_symbols() == expected);
}

TEST_CASE("vocab size limits the number of merges", "[bpe]") {
  auto sequence = {"set", "new", "new", "renew", "reset", "renew"};
  const BytePairEncoding::TrainOptions options{.vocab_size = 11};

  REQUIRE(BytePairEncoding::train(sequence, options).merged_symbols().size() == 4);
}
