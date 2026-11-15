module;

#include <ankerl/unordered_dense.h>

#include "tokenizer_generated.h"

module wdym;

import std.compat;
import :containers;
import :normalization;
import :word_splitting;
import :tokenizer_file;
import :utf8;

using wdym::BytePairEncoding;
using wdym::pushBackIfEmptyOrNotEqualToLast;
using wdym::splitWords;
using wdym::StringKeyMap;
using wdym::StringKeySet;
namespace unordered_dense = ankerl::unordered_dense;

namespace {
// TODO: make use of multithreading
StringKeyMap<uint64_t> getWordToCount(
    wdym::TextStream texts,
    const wdym::Normalizer& normalizer,
    const wdym::BytePairEncodingSettings& settings
) {
  StringKeyMap<uint64_t> wordToCount;
  std::string normalizationBuffer;

  for (const auto text : texts) {
    const auto normalizedText = normalizer.normalize(text, normalizationBuffer);

    splitWords(normalizedText, settings.wordDelimiter, [&](std::string_view word) {
      if (auto it = wordToCount.find(word); it != wordToCount.end())
        ++it->second;
      else
        wordToCount.emplace(word, 1);
    });
  }

  return wordToCount;
}

struct Word {
  std::vector<uint32_t> symbols;
  uint64_t count;
};

struct QueuedPair {
  uint64_t pairKey;
  uint64_t count;

  bool operator<(const QueuedPair& other) const {
    return count == other.count ? pairKey > other.pairKey : count < other.count;
  }
};

uint32_t getLeftSymbolId(uint64_t pairKey) {
  return static_cast<uint32_t>(pairKey >> 32);
}

uint32_t getRightSymbolId(uint64_t pairKey) {
  // This simply drops the high 32 bits
  return static_cast<uint32_t>(pairKey);
}

uint64_t getPairKey(uint32_t left, uint32_t right) {
  return (uint64_t{left} << 32) | right;
}

std::string_view getNextCharacter(std::string_view text) {
  // TODO: extract into common function that normalization.cpp can also use
  const auto leadingByte = static_cast<unsigned char>(text[0]);
  return text.substr(0, std::max(1, std::countl_one(leadingByte)));
}

template <std::invocable<std::string_view> OnSymbol>
void splitWordSymbols(
    std::string_view word,
    const wdym::BytePairEncodingSettings& settings,
    OnSymbol&& onSymbol,
    bool yieldUnadjoinedBoundaryCharacters = false
) {
  const bool hasPrefix = !settings.wordPrefix.empty();
  const bool hasSuffix = !settings.wordSuffix.empty();

  if (hasPrefix && !settings.adjoinPrefix)
    onSymbol(settings.wordPrefix);

  bool isFirstCharacter = true;

  while (!word.empty()) {
    const auto character = getNextCharacter(word);
    word.remove_prefix(character.size());

    const bool hasAdjoinedPrefix = isFirstCharacter && hasPrefix && settings.adjoinPrefix;
    const bool hasAdjoinedSuffix = word.empty() && hasSuffix && settings.adjoinSuffix;
    isFirstCharacter = false;

    if (hasAdjoinedPrefix || hasAdjoinedSuffix) {
      std::string adjoinedSymbol;

      if (hasAdjoinedPrefix)
        adjoinedSymbol.append(settings.wordPrefix);

      adjoinedSymbol.append(character);

      if (hasAdjoinedSuffix)
        adjoinedSymbol.append(settings.wordSuffix);

      onSymbol(adjoinedSymbol);

      // It'd feel more natural if the first character were yielded after the
      // prefix-adjoined one, and the last character before the suffix-adjoined
      // one. But our usage doesn't require this
      if (yieldUnadjoinedBoundaryCharacters)
        onSymbol(character);
    } else {
      onSymbol(character);
    }
  }

  if (hasSuffix && !settings.adjoinSuffix)
    onSymbol(settings.wordSuffix);
}

// TODO: see if we can use tokenToId method instead
std::optional<uint32_t> findSymbolId(const StringKeySet& symbols, std::string_view symbol) {
  const auto it = symbols.find(symbol);
  if (it == symbols.end())
    return std::nullopt;

  return static_cast<uint32_t>(it - symbols.begin());
}

wdym::BytePairEncodingSettings getValidatedSettings(wdym::BytePairEncodingSettings settings) {
  if (const auto* markupFallback =
          std::get_if<wdym::MarkupToken>(&settings.unknownCharacterFallback);
      markupFallback != nullptr &&
      !std::ranges::contains(settings.markupTokens, markupFallback->text))
    throw std::invalid_argument(
        std::format(
            "unknownCharacterFallback is the markup token {:?}, which markupTokens doesn't list",
            markupFallback->text
        )
    );

  for (
      const auto& [name, text] :
      std::initializer_list<std::pair<std::string_view, std::string_view>>{
          {"wordDelimiter", settings.wordDelimiter},
          {"wordPrefix", settings.wordPrefix},
          {"wordSuffix", settings.wordSuffix},
      }
  )
    if (!wdym::isValidUtf8(text))
      throw std::invalid_argument(std::format("{} {:?} is not valid UTF-8", name, text));

  if (const auto* stringFallback = std::get_if<std::string>(&settings.unknownCharacterFallback);
      stringFallback != nullptr) {
    if (stringFallback->empty())
      throw std::invalid_argument("unknownCharacterFallback is an empty token");

    if (!wdym::isValidUtf8(*stringFallback))
      throw std::invalid_argument(
          std::format("unknownCharacterFallback {:?} is not valid UTF-8", *stringFallback)
      );
  }

  for (const std::string_view markupToken : settings.markupTokens) {
    if (markupToken.empty())
      throw std::invalid_argument("markupTokens contains an empty token");

    if (!wdym::isValidUtf8(markupToken))
      throw std::invalid_argument(
          std::format("markupTokens contains {:?}, which is not valid UTF-8", markupToken)
      );
  }

  // We deduplicate markup tokens but we also make sure that they yield stable
  // symbol ids no matter their order
  std::ranges::sort(settings.markupTokens);
  const auto duplicates = std::ranges::unique(settings.markupTokens);
  settings.markupTokens.erase(duplicates.begin(), duplicates.end());

  return settings;
}

class RankedMergeEncoder {
 public:
  RankedMergeEncoder(
      const wdym::BytePairEncodingSettings& settings,
      const StringKeySet& symbols,
      const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
      const std::vector<uint32_t>& symbolIdToTokenId,
      std::optional<uint32_t> fallbackTokenId
  )
      : settings_(settings),
        symbols_(symbols),
        pairKeyToMergedSymbolId_(pairKeyToMergedSymbolId),
        symbolIdToTokenId_(symbolIdToTokenId),
        fallbackTokenId_(fallbackTokenId) {}

  void encode(std::string_view word, std::vector<uint32_t>& tokenIds) {
    encodedSymbolIds_.clear();

    splitWordSymbols(word, settings_, [this](std::string_view symbol) {
      // TODO: be mindful of case where prefix + character is unknown but
      // character by itself is known
      if (const auto symbolId = findSymbolId(symbols_, symbol))
        encodedSymbolIds_.push_back(*symbolId);
      else if (std::holds_alternative<wdym::ByteFallback>(settings_.unknownCharacterFallback))
        for (const char byte : symbol)
          encodedSymbolIds_.push_back(*findSymbolId(symbols_, std::string_view(&byte, 1)));
      else if (fallbackTokenId_.has_value())
        encodedSymbolIds_.push_back(UNKNOWN_CHARACTER_SYMBOL_ID);
    });

    // TODO: weird no-arg method...
    mergeSymbolIds();
    for (const auto symbolId : encodedSymbolIds_)
      tokenIds.push_back(
          symbolId == UNKNOWN_CHARACTER_SYMBOL_ID ? *fallbackTokenId_ : symbolIdToTokenId_[symbolId]
      );
  }

 private:
  static constexpr uint32_t UNKNOWN_CHARACTER_SYMBOL_ID = std::numeric_limits<uint32_t>::max();
  static constexpr uint32_t NO_MERGE_RANK = std::numeric_limits<uint32_t>::max();

  struct PairMerge {
    uint32_t rank;
    uint32_t mergedSymbolId;
  };

  PairMerge getPairMerge(uint32_t leftSymbolId, uint32_t rightSymbolId) const {
    const auto it = pairKeyToMergedSymbolId_.find(getPairKey(leftSymbolId, rightSymbolId));
    if (it == pairKeyToMergedSymbolId_.end())
      return {.rank = NO_MERGE_RANK, .mergedSymbolId = 0};

    return {
        .rank = static_cast<uint32_t>(it - pairKeyToMergedSymbolId_.begin()),
        .mergedSymbolId = it->second,
    };
  }

  void mergeSymbolIds() {
    adjacentPairMerges_.clear();
    for (size_t symbolIndex = 0; symbolIndex + 1 < encodedSymbolIds_.size(); ++symbolIndex)
      adjacentPairMerges_.push_back(
          getPairMerge(encodedSymbolIds_[symbolIndex], encodedSymbolIds_[symbolIndex + 1])
      );

    const auto findLowestRankMerge = [this] {
      return std::ranges::min_element(adjacentPairMerges_, {}, &PairMerge::rank);
    };

    for (
        auto lowestRankMerge = findLowestRankMerge();
        lowestRankMerge != adjacentPairMerges_.end() && lowestRankMerge->rank != NO_MERGE_RANK;
        lowestRankMerge = findLowestRankMerge()
    ) {
      const auto symbolIndex = static_cast<size_t>(lowestRankMerge - adjacentPairMerges_.begin());

      // previous pair
      if (symbolIndex >= 1)
        adjacentPairMerges_[symbolIndex - 1] =
            getPairMerge(encodedSymbolIds_[symbolIndex - 1], lowestRankMerge->mergedSymbolId);

      // next pair
      if (symbolIndex + 2 < encodedSymbolIds_.size())
        adjacentPairMerges_[symbolIndex + 1] =
            getPairMerge(lowestRankMerge->mergedSymbolId, encodedSymbolIds_[symbolIndex + 2]);

      encodedSymbolIds_[symbolIndex] = lowestRankMerge->mergedSymbolId;
      encodedSymbolIds_.erase(encodedSymbolIds_.begin() + symbolIndex + 1);
      adjacentPairMerges_.erase(lowestRankMerge);
    }
  }

  const wdym::BytePairEncodingSettings& settings_;
  const StringKeySet& symbols_;
  const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId_;
  const std::vector<uint32_t>& symbolIdToTokenId_;
  const std::optional<uint32_t> fallbackTokenId_;

  std::vector<uint32_t> encodedSymbolIds_;
  std::vector<PairMerge> adjacentPairMerges_;
};

StringKeyMap<uint32_t> buildWordToWholeWordSymbolId(
    const wdym::BytePairEncodingSettings& settings,
    const StringKeySet& symbols
) {
  StringKeyMap<uint32_t> wordToWholeWordSymbolId;

  for (uint32_t symbolId = 0; symbolId < symbols.size(); ++symbolId) {
    const std::string_view symbol = symbols.values()[symbolId];

    if (symbol.size() > settings.wordPrefix.size() + settings.wordSuffix.size() &&
        symbol.starts_with(settings.wordPrefix) &&
        symbol.ends_with(settings.wordSuffix)) {
      std::string_view word = symbol;
      word.remove_prefix(settings.wordPrefix.size());
      word.remove_suffix(settings.wordSuffix.size());
      wordToWholeWordSymbolId.emplace(word, symbolId);
    }
  }

  return wordToWholeWordSymbolId;
}

StringKeyMap<uint32_t> buildMarkupTokenToTokenId(
    const wdym::BytePairEncodingSettings& settings,
    const StringKeySet& symbols
) {
  StringKeyMap<uint32_t> markupTokenToTokenId;

  for (const std::string_view markupToken : settings.markupTokens)
    markupTokenToTokenId.emplace(
        markupToken, static_cast<uint32_t>(symbols.size() + markupTokenToTokenId.size())
    );

  return markupTokenToTokenId;
}

std::vector<uint32_t> buildSymbolIdToTokenId(const StringKeySet& symbols) {
  std::vector<uint32_t> symbolIdToTokenId(symbols.size());

  for (uint32_t symbolId = 0; symbolId < symbols.size(); ++symbolId)
    symbolIdToTokenId[symbolId] = symbolId;

  return symbolIdToTokenId;
}

std::vector<wdym::TokenSource> buildTokenIdToTokenSource(
    const StringKeySet& symbols,
    const std::vector<uint32_t>& symbolIdToTokenId,
    const StringKeyMap<uint32_t>& markupTokenToTokenId
) {
  std::vector<wdym::TokenSource> tokenIdToTokenSource(symbols.size() + markupTokenToTokenId.size());

  for (uint32_t symbolId = 0; symbolId < symbols.size(); ++symbolId)
    tokenIdToTokenSource[symbolIdToTokenId[symbolId]] = {.isMarkupToken = false, .index = symbolId};

  for (uint32_t markupIndex = 0; markupIndex < markupTokenToTokenId.size(); ++markupIndex)
    tokenIdToTokenSource[markupTokenToTokenId.values()[markupIndex].second] = {
        .isMarkupToken = true,
        .index = markupIndex,
    };

  return tokenIdToTokenSource;
}

void assertInvalidUtf8IsReplaced(const wdym::Normalizer& normalizer) {
  if (std::holds_alternative<wdym::KeepInvalidUtf8>(normalizer.invalidUtf8Fallback))
    throw std::invalid_argument("normalization for BytePairEncoding cannot keep invalid UTF-8");
}

class BytePairEncodingTrainer {
 public:
  explicit BytePairEncodingTrainer(const wdym::BytePairEncodingSettings& settings)
      : settings_(settings) {}

  void registerAlphabet(const StringKeyMap<uint64_t>& wordToCount) {
    // Instead of immediately generating symbol IDs for each character, we first
    // collect the characters into a set and generate the IDs from it once sorted.
    // This gives characters the same IDs no matter the order of words, lines or
    // input files, and consequently yields the same merges.
    StringKeySet alphabet;
    const auto addToAlphabet = [&](std::string_view symbol) {
      if (!alphabet.contains(symbol))
        alphabet.emplace(symbol);
    };

    if (std::holds_alternative<wdym::ByteFallback>(settings_.unknownCharacterFallback))
      for (int byte = 0; byte < 256; ++byte)
        addToAlphabet(std::string(1, static_cast<char>(byte)));
    else if (const auto* token = std::get_if<std::string>(&settings_.unknownCharacterFallback))
      addToAlphabet(*token);

    for (const auto& [word, _] : wordToCount)
      splitWordSymbols(word, settings_, addToAlphabet, true);

    auto sortedAlphabet = std::move(alphabet).extract();
    std::ranges::sort(sortedAlphabet);

    for (const auto& symbol : sortedAlphabet)
      setAndGetSymbolId(symbol);
  }

  void buildInitialPairs(const StringKeyMap<uint64_t>& wordToCount) {
    words_.reserve(wordToCount.size());

    for (const auto& [text, count] : wordToCount) {
      const auto wordIndex = words_.size();
      auto& word = words_.emplace_back(Word{.symbols = {}, .count = count});

      // This is the least upper bound on the total number of unicode characters, which occurs when
      // all characters are single-byte unicode characters and there is a separate prefix and suffix
      word.symbols.reserve(text.size() + 2);
      splitWordSymbols(text, settings_, [&](std::string_view symbol) {
        word.symbols.push_back(setAndGetSymbolId(symbol));
      });

      for (size_t symbolIndex = 0; symbolIndex + 1 < word.symbols.size(); ++symbolIndex) {
        const auto key = getPairKey(word.symbols[symbolIndex], word.symbols[symbolIndex + 1]);
        pairToCount_[key] += word.count;

        pushBackIfEmptyOrNotEqualToLast(pairToWordIndices_[key], static_cast<uint32_t>(wordIndex));
      }
    }

    for (const auto& [pairKey, count] : pairToCount_)
      pairCountPriorityQueue_.emplace(QueuedPair{.pairKey = pairKey, .count = count});
  }

  std::optional<uint64_t> popMostFrequentPair() {
    while (!pairCountPriorityQueue_.empty()) {
      auto queuedPair = pairCountPriorityQueue_.top();
      pairCountPriorityQueue_.pop();

      if (auto it = pairToCount_.find(queuedPair.pairKey);
          it == pairToCount_.end() || it->second != queuedPair.count || it->second == 0) {
        if (it != pairToCount_.end() && it->second != 0)
          pairCountPriorityQueue_.emplace(
              QueuedPair{.pairKey = queuedPair.pairKey, .count = it->second}
          );
      } else {
        return queuedPair.pairKey;
      }
    }

    return std::nullopt;
  }

  void mergePair(uint64_t pairKey) {
    auto leftSymbolId = getLeftSymbolId(pairKey);
    auto rightSymbolId = getRightSymbolId(pairKey);

    auto pairSymbol = symbols.values()[leftSymbolId] + symbols.values()[rightSymbolId];
    auto pairSymbolId = setAndGetSymbolId(pairSymbol);

    unordered_dense::set<uint64_t> pairsWithUpdatedCounts;

    const auto replacePair = [&](uint64_t oldPairKey,
                                 uint64_t newPairKey,
                                 uint64_t parentWordCount,
                                 uint32_t wordIndex) {
      auto& oldPairCount = pairToCount_.at(oldPairKey);
      oldPairCount -= parentWordCount;

      // MEMORY OPTIMIZATION: Training works without it
      if (oldPairCount == 0)
        pairToCount_.erase(oldPairKey);

      pairToCount_[newPairKey] += parentWordCount;
      pairsWithUpdatedCounts.insert(newPairKey);

      pushBackIfEmptyOrNotEqualToLast(pairToWordIndices_[newPairKey], wordIndex);
    };

    // Replace neighboring pairs that overlap the one we want to merge in all
    // words that contain it
    const auto wordIndices = std::move(pairToWordIndices_[pairKey]);
    for (const auto wordIndex : wordIndices) {
      auto& word = words_[wordIndex];

      for (size_t symbolIndex = 0; symbolIndex + 1 < word.symbols.size(); ++symbolIndex) {
        if (word.symbols[symbolIndex] == leftSymbolId &&
            word.symbols[symbolIndex + 1] == rightSymbolId) {
          // previous pair
          if (symbolIndex >= 1) {
            auto oldPreviousPairKey =
                getPairKey(word.symbols[symbolIndex - 1], word.symbols[symbolIndex]);
            auto newPreviousPairKey = getPairKey(word.symbols[symbolIndex - 1], pairSymbolId);

            replacePair(oldPreviousPairKey, newPreviousPairKey, word.count, wordIndex);
          }
          // next pair
          if (symbolIndex + 2 < word.symbols.size()) {
            auto oldNextPairKey =
                getPairKey(word.symbols[symbolIndex + 1], word.symbols[symbolIndex + 2]);
            auto newNextPairKey = getPairKey(pairSymbolId, word.symbols[symbolIndex + 2]);

            replacePair(oldNextPairKey, newNextPairKey, word.count, wordIndex);
          }

          // Overwrite the symbol and erase the next subsumed one
          word.symbols[symbolIndex] = pairSymbolId;
          word.symbols.erase(word.symbols.begin() + symbolIndex + 1);
        }
      }
    }

    pairToCount_.erase(pairKey);
    pairToWordIndices_.erase(pairKey);

    pairKeyToMergedSymbolId.emplace(pairKey, pairSymbolId);

    for (const auto pairKeyWithUpdatedCount : pairsWithUpdatedCounts)
      if (auto it = pairToCount_.find(pairKeyWithUpdatedCount); it != pairToCount_.end())
        pairCountPriorityQueue_.emplace(
            QueuedPair{.pairKey = pairKeyWithUpdatedCount, .count = it->second}
        );
  }

  StringKeySet symbols;
  unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId;

 private:
  uint32_t setAndGetSymbolId(const std::string_view symbol) {
    auto it = symbols.find(symbol);
    if (it == symbols.end())
      it = symbols.emplace(symbol).first;

    return static_cast<uint32_t>(it - symbols.begin());
  }

  const wdym::BytePairEncodingSettings& settings_;

  std::vector<Word> words_;
  unordered_dense::map<uint64_t, uint64_t> pairToCount_;
  unordered_dense::map<uint64_t, std::vector<uint32_t>> pairToWordIndices_;
  std::priority_queue<QueuedPair> pairCountPriorityQueue_;
};
}  // namespace

BytePairEncoding::BytePairEncoding(
    Normalizer normalizer,
    BytePairEncodingSettings settings,
    StringKeySet symbols,
    unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId,
    StringKeyMap<uint32_t> markupTokenToTokenId,
    std::vector<uint32_t> symbolIdToTokenId
)
    : TokenizerBase(std::move(normalizer)),
      settings_(std::move(settings)),
      symbols_(std::move(symbols)),
      pairKeyToMergedSymbolId_(std::move(pairKeyToMergedSymbolId)),
      markupTokenToTokenId_(std::move(markupTokenToTokenId)),
      symbolIdToTokenId_(std::move(symbolIdToTokenId)),
      tokenIdToTokenSource_(
          buildTokenIdToTokenSource(symbols_, symbolIdToTokenId_, markupTokenToTokenId_)
      ),
      wordToWholeWordSymbolId_(buildWordToWholeWordSymbolId(settings_, symbols_)) {}

std::span<const uint32_t> BytePairEncoding::WordCache::find(std::string_view word, uint64_t hash) {
  static_assert(WAYS_PER_SET == 2);

  if (word.size() > MAX_WORD_SIZE)
    return {};

  auto& set = getSet(hash);

  for (size_t way = 0; way < set.entries.size(); ++way) {
    auto& entry = set.entries[way];
    if (entry.tokenIdCount == 0 ||
        entry.wordSize != word.size() ||
        !std::ranges::equal(std::span(entry.word).first(word.size()), word))
      continue;

    entry.isMostRecentlyUsed = true;
    set.entries[1 - way].isMostRecentlyUsed = false;

    return std::span(entry.tokenIds).first(entry.tokenIdCount);
  }

  return {};
}

void BytePairEncoding::WordCache::insert(
    std::string_view word,
    uint64_t hash,
    std::span<const uint32_t> tokenIds
) {
  static_assert(WAYS_PER_SET == 2);

  if (word.size() > MAX_WORD_SIZE || tokenIds.empty() || tokenIds.size() > MAX_TOKEN_ID_COUNT)
    return;

  auto& set = getSet(hash);

  const auto& firstEntry = set.entries[0];
  const auto& secondEntry = set.entries[1];
  const size_t insertionWay = firstEntry.tokenIdCount == 0    ? 0
                              : secondEntry.tokenIdCount == 0 ? 1
                              : firstEntry.isMostRecentlyUsed ? 1
                                                              : 0;

  auto& insertionEntry = set.entries[insertionWay];
  insertionEntry.wordSize = static_cast<uint8_t>(word.size());
  insertionEntry.tokenIdCount = static_cast<uint8_t>(tokenIds.size());
  insertionEntry.isMostRecentlyUsed = true;

  std::ranges::copy(word, insertionEntry.word.begin());
  std::ranges::copy(tokenIds, insertionEntry.tokenIds.begin());

  set.entries[1 - insertionWay].isMostRecentlyUsed = false;
}

BytePairEncoding BytePairEncoding::trainFromTexts(
    TextStream texts,
    const TrainOptions& options,
    const Settings& settings,
    Normalizer normalizer
) {
  const auto validatedSettings = getValidatedSettings(settings);
  assertInvalidUtf8IsReplaced(normalizer);

  BytePairEncodingTrainer trainer(validatedSettings);

  const auto vocabularySize = [&] {
    return trainer.symbols.size() + validatedSettings.markupTokens.size();
  };

  // MEMORY OPTIMIZATION: separate scope to free up wordToCount once done with it
  {
    auto wordToCount = getWordToCount(std::move(texts), normalizer, validatedSettings);

    trainer.registerAlphabet(wordToCount);

    if (std::cmp_greater(vocabularySize(), options.maxVocabularySize))
      throw std::invalid_argument(
          std::format(
              "maxVocabularySize {} is too small: the corpus and markup tokens need at least {}",
              options.maxVocabularySize,
              vocabularySize()
          )
      );

    trainer.buildInitialPairs(wordToCount);
  }

  while (std::cmp_less(vocabularySize(), options.maxVocabularySize)) {
    auto mostFrequentPairKey = trainer.popMostFrequentPair();
    if (!mostFrequentPairKey.has_value())
      break;

    trainer.mergePair(mostFrequentPairKey.value());
  }

  auto markupTokenToTokenId = buildMarkupTokenToTokenId(validatedSettings, trainer.symbols);
  auto symbolIdToTokenId = buildSymbolIdToTokenId(trainer.symbols);

  return BytePairEncoding(
      std::move(normalizer),
      validatedSettings,
      std::move(trainer.symbols),
      std::move(trainer.pairKeyToMergedSymbolId),
      std::move(markupTokenToTokenId),
      std::move(symbolIdToTokenId)
  );
}

std::vector<uint32_t> BytePairEncoding::encode(std::string_view text) const {
  std::vector<uint32_t> tokenIds;
  encode(text, tokenIds);
  return tokenIds;
}

void BytePairEncoding::encode(
    std::string_view text,
    std::vector<uint32_t>& tokenIds,
    std::optional<WordCache&> cache
) const {
  std::string normalizationBuffer;
  encodeNormalized(normalizer_.normalize(text, normalizationBuffer), tokenIds, cache);
}

std::vector<uint32_t> BytePairEncoding::encodeNormalized(std::string_view text) const {
  std::vector<uint32_t> tokenIds;
  encodeNormalized(text, tokenIds);
  return tokenIds;
}

void BytePairEncoding::encodeNormalized(
    std::string_view text,
    std::vector<uint32_t>& tokenIds,
    std::optional<WordCache&> cache
) const {
  RankedMergeEncoder rankedMergeEncoder(
      settings_, symbols_, pairKeyToMergedSymbolId_, symbolIdToTokenId_, findFallbackTokenId()
  );

  splitWords(text, settings_.wordDelimiter, [&](std::string_view word) {
    const auto wordHash = wordToWholeWordSymbolId_.hash_for(word);

    if (const auto it = wordToWholeWordSymbolId_.find(word, wordHash);
        it != wordToWholeWordSymbolId_.end()) {
      tokenIds.push_back(symbolIdToTokenId_[it->second]);
      return;
    }

    if (!cache.has_value()) {
      rankedMergeEncoder.encode(word, tokenIds);
      return;
    }

    if (const auto cachedTokenIds = cache->find(word, wordHash.m_mixed_hash);
        !cachedTokenIds.empty()) {
      tokenIds.append_range(cachedTokenIds);
      return;
    }

    const auto wordFirstTokenIdIndex = tokenIds.size();
    rankedMergeEncoder.encode(word, tokenIds);
    cache->insert(word, wordHash.m_mixed_hash, std::span(tokenIds).subspan(wordFirstTokenIdIndex));
  });
}

std::vector<uint32_t> BytePairEncoding::encodeParts(
    std::span<const TextOrMarkupToken> parts
) const {
  std::vector<uint32_t> tokenIds;
  encodeParts(parts, tokenIds);
  return tokenIds;
}

void BytePairEncoding::encodeParts(
    std::span<const TextOrMarkupToken> parts,
    std::vector<uint32_t>& tokenIds,
    std::optional<WordCache&> cache
) const {
  for (const TextOrMarkupToken& part : parts) {
    if (const auto* markupToken = std::get_if<MarkupToken>(&part)) {
      const auto tokenId = markupTokenToId(markupToken->text);
      if (!tokenId.has_value())
        throw std::invalid_argument(
            std::format("{:?} is not one of the tokenizer's markup tokens", markupToken->text)
        );

      tokenIds.push_back(*tokenId);
    } else {
      encode(std::get<std::string>(part), tokenIds, cache);
    }
  }
}

std::string BytePairEncoding::decode(
    std::span<const uint32_t> tokenIds,
    const BytePairEncodingDecodeOptions& options
) const {
  std::string text;
  decode(tokenIds, text, options);
  return text;
}

void BytePairEncoding::decode(
    std::span<const uint32_t> tokenIds,
    std::string& text,
    const BytePairEncodingDecodeOptions& options
) const {
  for (const TextOrMarkupToken& part : decodeParts(tokenIds, options)) {
    if (const auto* markupToken = std::get_if<MarkupToken>(&part))
      text.append(markupToken->text);
    else
      text.append(std::get<std::string>(part));
  }
}

std::vector<wdym::TextOrMarkupToken> BytePairEncoding::decodeParts(
    std::span<const uint32_t> tokenIds,
    const BytePairEncodingDecodeOptions& options
) const {
  std::vector<TextOrMarkupToken> parts;
  std::string symbolText;

  const auto appendTextPart = [&](bool removeTrailingWordSuffix) {
    if (symbolText.empty())
      return;

    const bool removeLeadingWordPrefix = parts.empty() && options.removeLeadingWordPrefix;
    std::string text = decodeSymbolText(
        symbolText,
        removeLeadingWordPrefix,
        removeTrailingWordSuffix,
        options.restoreWordDelimiters,
        options.invalidUtf8Replacement
    );
    if (!text.empty())
      parts.emplace_back(std::move(text));

    symbolText.clear();
  };

  for (const uint32_t tokenId : tokenIds) {
    const auto& token = tokenIdToTokenSource_.at(tokenId);

    if (token.isMarkupToken) {
      appendTextPart(false);
      parts.emplace_back(MarkupToken{markupTokenToTokenId_.values()[token.index].first});
    } else {
      symbolText.append(symbols_.values()[token.index]);
    }
  }

  appendTextPart(options.removeTrailingWordSuffix);
  return parts;
}

std::string BytePairEncoding::decodeSymbolText(
    std::string_view symbolText,
    bool removeLeadingWordPrefix,
    bool removeTrailingWordSuffix,
    bool restoreWordDelimiters,
    std::string_view invalidUtf8Replacement
) const {
  if (removeLeadingWordPrefix && symbolText.starts_with(settings_.wordPrefix))
    symbolText.remove_prefix(settings_.wordPrefix.size());

  if (removeTrailingWordSuffix && symbolText.ends_with(settings_.wordSuffix))
    symbolText.remove_suffix(settings_.wordSuffix.size());

  std::string text;
  const std::string wordSuffixAndPrefix = settings_.wordSuffix + settings_.wordPrefix;

  if (restoreWordDelimiters && !wordSuffixAndPrefix.empty())
    text.append_range(
        symbolText |
        std::views::split(std::string_view(wordSuffixAndPrefix)) |
        std::views::join_with(std::string_view(settings_.wordDelimiter))
    );
  else
    text = symbolText;

  std::string replacedText;
  if (replaceInvalidUtf8(text, invalidUtf8Replacement, replacedText))
    text = std::move(replacedText);

  return text;
}

std::optional<uint32_t> BytePairEncoding::tokenToId(std::string_view token) const {
  if (const auto symbolId = findSymbolId(symbols_, token))
    return symbolIdToTokenId_[*symbolId];

  return std::nullopt;
}

std::optional<uint32_t> BytePairEncoding::markupTokenToId(std::string_view token) const {
  const auto it = markupTokenToTokenId_.find(token);
  if (it == markupTokenToTokenId_.end())
    return std::nullopt;

  return it->second;
}

std::optional<uint32_t> BytePairEncoding::findFallbackTokenId() const {
  if (const auto* markupFallback = std::get_if<MarkupToken>(&settings_.unknownCharacterFallback))
    return markupTokenToId(markupFallback->text);

  if (const auto* stringFallback = std::get_if<std::string>(&settings_.unknownCharacterFallback))
    return tokenToId(*stringFallback);

  return std::nullopt;
}

std::vector<std::byte> BytePairEncoding::toBytes() const {
  flatbuffers::FlatBufferBuilder builder;

  auto unknownCharacterFallbackType = schema::UnknownCharacterFallback::NONE;
  flatbuffers::Offset<void> unknownCharacterFallback;
  if (std::holds_alternative<ByteFallback>(settings_.unknownCharacterFallback)) {
    unknownCharacterFallbackType = schema::UnknownCharacterFallback::ByteFallback;
    unknownCharacterFallback = schema::CreateByteFallback(builder).Union();
  } else if (
      const auto* symbolFallback = std::get_if<std::string>(&settings_.unknownCharacterFallback)
  ) {
    unknownCharacterFallbackType = schema::UnknownCharacterFallback::SymbolFallback;
    unknownCharacterFallback = builder.CreateString(*symbolFallback).Union();
  } else if (
      const auto* markupFallback = std::get_if<MarkupToken>(&settings_.unknownCharacterFallback)
  ) {
    unknownCharacterFallbackType = schema::UnknownCharacterFallback::MarkupTokenFallback;
    unknownCharacterFallback = builder.CreateString(markupFallback->text).Union();
  }

  const auto settings = schema::CreateBytePairEncodingSettings(
      builder,
      builder.CreateString(settings_.wordDelimiter),
      builder.CreateString(settings_.wordPrefix),
      builder.CreateString(settings_.wordSuffix),
      settings_.adjoinPrefix,
      settings_.adjoinSuffix,
      unknownCharacterFallbackType,
      unknownCharacterFallback
  );

  std::string symbolBytes;
  std::vector<uint32_t> symbolOffsets = {0};
  for (const std::string& symbol : symbols_.values()) {
    symbolBytes.append(symbol);
    symbolOffsets.push_back(static_cast<uint32_t>(symbolBytes.size()));
  }

  std::vector<schema::Merge> merges;
  for (const auto& [pairKey, mergedSymbolId] : pairKeyToMergedSymbolId_)
    merges.emplace_back(getLeftSymbolId(pairKey), getRightSymbolId(pairKey), mergedSymbolId);

  std::vector<flatbuffers::Offset<schema::MarkupToken>> markupTokens;
  for (const auto& [markupToken, tokenId] : markupTokenToTokenId_)
    markupTokens.push_back(
        schema::CreateMarkupToken(builder, builder.CreateString(markupToken), tokenId)
    );

  const auto model = schema::CreateBytePairEncoding(
      builder,
      settings,
      builder.CreateVector(
          reinterpret_cast<const uint8_t*>(symbolBytes.data()), symbolBytes.size()
      ),
      builder.CreateVector(symbolOffsets),
      builder.CreateVectorOfStructs(merges),
      builder.CreateVector(symbolIdToTokenId_),
      builder.CreateVector(markupTokens)
  );
  return finishTokenizerFile(builder, normalizer_, schema::Model::BytePairEncoding, model.Union());
}

BytePairEncoding BytePairEncoding::fromBytes(std::span<const std::byte> bytes) {
  if (reinterpret_cast<uintptr_t>(bytes.data()) % alignof(std::max_align_t) != 0) {
    const std::vector<std::byte> alignedBytes(bytes.begin(), bytes.end());
    return fromBytes(alignedBytes);
  }

  const schema::Tokenizer& tokenizer = getVerifiedTokenizerFile(bytes);
  const schema::BytePairEncoding* model = tokenizer.model_as_BytePairEncoding();
  if (model == nullptr || model->settings() == nullptr)
    throw std::invalid_argument("the tokenizer file doesn't hold a complete BytePairEncoding");

  BytePairEncodingSettings settings;
  const schema::BytePairEncodingSettings* settingsTable = model->settings();
  settings.wordDelimiter = toStringView(settingsTable->word_delimiter());
  settings.wordPrefix = toStringView(settingsTable->word_prefix());
  settings.wordSuffix = toStringView(settingsTable->word_suffix());
  settings.adjoinPrefix = settingsTable->adjoin_prefix();
  settings.adjoinSuffix = settingsTable->adjoin_suffix();

  if (settingsTable->unknown_character_fallback_as_ByteFallback() != nullptr)
    settings.unknownCharacterFallback = ByteFallback{};
  else if (
      const auto* symbolFallback = settingsTable->unknown_character_fallback_as_SymbolFallback()
  )
    settings.unknownCharacterFallback = std::string(symbolFallback->string_view());
  else if (
      const auto* markupFallback =
          settingsTable->unknown_character_fallback_as_MarkupTokenFallback()
  )
    settings.unknownCharacterFallback = MarkupToken{std::string(markupFallback->string_view())};
  else if (
      settingsTable->unknown_character_fallback_type() != schema::UnknownCharacterFallback::NONE
  )
    throw std::invalid_argument("the tokenizer file has an unreadable unknownCharacterFallback");

  Normalizer normalizer = readNormalizer(tokenizer);
  assertInvalidUtf8IsReplaced(normalizer);

  const std::string_view symbolBytes = toStringView(model->symbol_bytes());
  const std::span<const uint32_t> symbolOffsets = toSpan(model->symbol_offsets());
  if (symbolOffsets.empty() ||
      symbolOffsets.front() != 0 ||
      symbolOffsets.back() != symbolBytes.size() ||
      std::ranges::adjacent_find(symbolOffsets, std::greater_equal<>()) != symbolOffsets.end())
    throw std::invalid_argument("the tokenizer file has inconsistent symbol offsets");

  StringKeySet symbols;
  for (size_t symbolId = 0; symbolId + 1 < symbolOffsets.size(); ++symbolId) {
    const auto symbol = symbolBytes.substr(
        symbolOffsets[symbolId], symbolOffsets[symbolId + 1] - symbolOffsets[symbolId]
    );
    if (!symbols.emplace(symbol).second)
      throw std::invalid_argument(
          std::format("the tokenizer file has a duplicate symbol {:?}", symbol)
      );
  }

  unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId;
  if (model->merges() != nullptr) {
    for (const schema::Merge* merge : *model->merges()) {
      const uint32_t leftSymbolId = merge->left_symbol_id();
      const uint32_t rightSymbolId = merge->right_symbol_id();
      const uint32_t mergedSymbolId = merge->merged_symbol_id();

      if (leftSymbolId >= symbols.size() ||
          rightSymbolId >= symbols.size() ||
          mergedSymbolId >= symbols.size() ||
          symbols.values()[mergedSymbolId] !=
              symbols.values()[leftSymbolId] + symbols.values()[rightSymbolId])
        throw std::invalid_argument("the tokenizer file has an inconsistent merge");

      if (!pairKeyToMergedSymbolId.emplace(getPairKey(leftSymbolId, rightSymbolId), mergedSymbolId)
               .second)
        throw std::invalid_argument("the tokenizer file has a duplicate merge");
    }
  }

  const std::span<const uint32_t> symbolIdToTokenId = toSpan(model->symbol_id_to_token_id());
  if (symbolIdToTokenId.size() != symbols.size())
    throw std::invalid_argument("the tokenizer file doesn't have a token ID for every symbol");

  StringKeyMap<uint32_t> markupTokenToTokenId;
  if (model->markup_tokens() != nullptr) {
    for (const schema::MarkupToken* markupToken : *model->markup_tokens()) {
      const std::string_view text = toStringView(markupToken->text());
      if (!markupTokenToTokenId.emplace(text, markupToken->token_id()).second)
        throw std::invalid_argument(
            std::format("the tokenizer file has a duplicate markup token {:?}", text)
        );

      settings.markupTokens.emplace_back(text);
    }
  }

  const size_t tokenCount = symbols.size() + markupTokenToTokenId.size();
  std::vector<bool> areTokenIdsUsed(tokenCount, false);
  // TODO: refactor to allow token ids outside of [0, token count] range
  const auto checkIsTokenIdUsed = [&](uint32_t tokenId) {
    if (tokenId >= tokenCount || areTokenIdsUsed[tokenId])
      throw std::invalid_argument("the tokenizer file's token IDs aren't dense and unique");

    areTokenIdsUsed[tokenId] = true;
  };

  for (const uint32_t tokenId : symbolIdToTokenId)
    checkIsTokenIdUsed(tokenId);

  for (const auto& [_, tokenId] : markupTokenToTokenId)
    checkIsTokenIdUsed(tokenId);

  settings = getValidatedSettings(std::move(settings));

  if (const auto* symbolFallback = std::get_if<std::string>(&settings.unknownCharacterFallback);
      symbolFallback != nullptr && !symbols.contains(*symbolFallback))
    throw std::invalid_argument(
        std::format(
            "the tokenizer file's symbol fallback {:?} isn't one of its symbols", *symbolFallback
        )
    );

  if (std::holds_alternative<ByteFallback>(settings.unknownCharacterFallback))
    for (int byte = 0; byte < 256; ++byte)
      if (!symbols.contains(std::string(1, static_cast<char>(byte))))
        throw std::invalid_argument(
            "the tokenizer file uses byte fallback without all 256 byte symbols"
        );

  return BytePairEncoding(
      std::move(normalizer),
      std::move(settings),
      std::move(symbols),
      std::move(pairKeyToMergedSymbolId),
      std::move(markupTokenToTokenId),
      std::vector<uint32_t>(symbolIdToTokenId.begin(), symbolIdToTokenId.end())
  );
}

std::vector<std::pair<std::string, std::string>> BytePairEncoding::merges() const {
  std::vector<std::pair<std::string, std::string>> result;

  result.reserve(pairKeyToMergedSymbolId_.size());
  for (const auto& [pairKey, _] : pairKeyToMergedSymbolId_) {
    auto leftSymbolId = getLeftSymbolId(pairKey);
    auto rightSymbolId = getRightSymbolId(pairKey);

    result.emplace_back(symbols_.values()[leftSymbolId], symbols_.values()[rightSymbolId]);
  }

  return result;
}
