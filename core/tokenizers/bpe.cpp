module;

#include <ankerl/unordered_dense.h>

module wdym;

import std.compat;
import :containers;
import :normalization;
import :utf8;

using wdym::BytePairEncoding;
using wdym::StringKeyMap;
using wdym::StringKeySet;
namespace unordered_dense = ankerl::unordered_dense;

namespace {
// Really wanted this function (and splitWordSymbols) to return a generator,
// which would be nicer to use at call sites. But generators can't be inlined
// (as opposed to this callback-form function) so there is a performance hit.
// To revisit...
template <std::invocable<std::string_view> OnWord>
void splitWords(std::string_view text, std::string_view delimiter, OnWord&& onWord) {
  if (delimiter.empty()) {
    if (!text.empty())
      onWord(text);

    return;
  }

  for (const auto word : std::views::split(text, delimiter))
    if (!word.empty())
      onWord(std::string_view(word));
}

// TODO: make use of multithreading
StringKeyMap<uint64_t> getWordToCount(
    wdym::TextStream texts,
    const wdym::BytePairEncodingSettings& settings
) {
  StringKeyMap<uint64_t> wordToCount;
  const wdym::Normalizer normalizer(settings.normalization);
  std::string normalizationBuffer;

  for (const auto text : texts) {
    const auto normalizedText = normalizer.normalize(text, normalizationBuffer);

    splitWords(normalizedText, settings.word_delimiter, [&](std::string_view word) {
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

inline uint32_t getLeftSymbolId(uint64_t pairKey) {
  return static_cast<uint32_t>(pairKey >> 32);
}

inline uint32_t getRightSymbolId(uint64_t pairKey) {
  // This simply drops the high 32 bits
  return static_cast<uint32_t>(pairKey);
}

inline uint64_t getPairKey(uint32_t left, uint32_t right) {
  return (uint64_t{left} << 32) | right;
}

template <typename T, typename U>
  requires std::same_as<std::remove_cvref_t<U>, T>
void pushBackIfEmptyOrNotEqualToLast(std::vector<T>& elements, U&& value) {
  if (elements.empty() || elements.back() != value)
    elements.push_back(std::forward<U>(value));
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
  const bool hasPrefix = !settings.word_prefix.empty();
  const bool hasSuffix = !settings.word_suffix.empty();

  if (hasPrefix && !settings.adjoin_prefix)
    onSymbol(settings.word_prefix);

  bool isFirstCharacter = true;

  while (!word.empty()) {
    const auto character = getNextCharacter(word);
    word.remove_prefix(character.size());

    const bool hasAdjoinedPrefix = isFirstCharacter && hasPrefix && settings.adjoin_prefix;
    const bool hasAdjoinedSuffix = word.empty() && hasSuffix && settings.adjoin_suffix;
    isFirstCharacter = false;

    if (hasAdjoinedPrefix || hasAdjoinedSuffix) {
      std::string adjoinedSymbol;

      if (hasAdjoinedPrefix)
        adjoinedSymbol.append(settings.word_prefix);

      adjoinedSymbol.append(character);

      if (hasAdjoinedSuffix)
        adjoinedSymbol.append(settings.word_suffix);

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

  if (hasSuffix && !settings.adjoin_suffix)
    onSymbol(settings.word_suffix);
}

// TODO: see if we can use token_to_id method instead
std::optional<uint32_t> findSymbolId(const StringKeySet& symbols, std::string_view symbol) {
  const auto it = symbols.find(symbol);
  if (it == symbols.end())
    return std::nullopt;

  return static_cast<uint32_t>(it - symbols.begin());
}

constexpr uint32_t NO_MERGE_RANK = std::numeric_limits<uint32_t>::max();

struct PairMerge {
  uint32_t rank;
  uint32_t mergedSymbolId;
};

PairMerge getPairMerge(
    const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
    uint32_t leftSymbolId,
    uint32_t rightSymbolId
) {
  const auto it = pairKeyToMergedSymbolId.find(getPairKey(leftSymbolId, rightSymbolId));
  if (it == pairKeyToMergedSymbolId.end())
    return {.rank = NO_MERGE_RANK, .mergedSymbolId = 0};

  return {
      .rank = static_cast<uint32_t>(it - pairKeyToMergedSymbolId.begin()),
      .mergedSymbolId = it->second,
  };
}

wdym::BytePairEncodingSettings getValidatedSettings(wdym::BytePairEncodingSettings settings) {
  if (std::holds_alternative<wdym::KeepInvalidUtf8>(settings.normalization.invalid_utf8_fallback))
    throw std::invalid_argument("normalization for BytePairEncoding cannot keep invalid UTF-8");

  if (const auto* markupFallback =
          std::get_if<wdym::MarkupToken>(&settings.unknown_character_fallback);
      markupFallback != nullptr &&
      !std::ranges::contains(settings.markup_tokens, markupFallback->text))
    throw std::invalid_argument(
        std::format(
            "unknown_character_fallback is the markup token {:?}, which markup_tokens doesn't list",
            markupFallback->text
        )
    );

  if (const auto* stringFallback = std::get_if<std::string>(&settings.unknown_character_fallback);
      stringFallback != nullptr && stringFallback->empty())
    throw std::invalid_argument("unknown_character_fallback is an empty token");

  if (const auto* byteFallback =
          std::get_if<wdym::ByteFallback>(&settings.unknown_character_fallback);
      byteFallback != nullptr && !wdym::isValidUtf8(byteFallback->invalid_utf8_replacement))
    throw std::invalid_argument(
        std::format(
            "unknown_character_fallback's invalid_utf8_replacement {:?} is not valid UTF-8",
            byteFallback->invalid_utf8_replacement
        )
    );

  for (const std::string_view markupToken : settings.markup_tokens)
    if (markupToken.empty())
      throw std::invalid_argument("markup_tokens contains an empty token");

  // We deduplicate markup tokens but we also make sure that they yield stable
  // symbol ids no matter their order
  std::ranges::sort(settings.markup_tokens);
  const auto duplicates = std::ranges::unique(settings.markup_tokens);
  settings.markup_tokens.erase(duplicates.begin(), duplicates.end());

  return settings;
}

// TODO: maybe rename to MergeReplayEncoder, in contrast to other encoding
// techniques trained with BPE
class BytePairEncodingWordEncoder {
 public:
  BytePairEncodingWordEncoder(
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

  void encode(std::string_view word, std::vector<uint32_t>& ids) {
    encodedSymbolIds_.clear();

    splitWordSymbols(word, settings_, [this](std::string_view symbol) {
      // TODO: be mindful of case where prefix + character is unknown but
      // character by itself is known
      if (const auto symbolId = findSymbolId(symbols_, symbol))
        encodedSymbolIds_.push_back(*symbolId);
      else if (std::holds_alternative<wdym::ByteFallback>(settings_.unknown_character_fallback))
        for (const char byte : symbol)
          encodedSymbolIds_.push_back(*findSymbolId(symbols_, std::string_view(&byte, 1)));
      else if (fallbackTokenId_.has_value())
        encodedSymbolIds_.push_back(UNKNOWN_CHARACTER_SYMBOL_ID);
    });

    // TODO: weird no-arg method...
    mergeSymbolIds();
    for (const auto symbolId : encodedSymbolIds_)
      ids.push_back(
          symbolId == UNKNOWN_CHARACTER_SYMBOL_ID ? *fallbackTokenId_ : symbolIdToTokenId_[symbolId]
      );
  }

 private:
  static constexpr uint32_t UNKNOWN_CHARACTER_SYMBOL_ID = std::numeric_limits<uint32_t>::max();

  void mergeSymbolIds() {
    adjacentPairMerges_.clear();
    for (size_t symbolIndex = 0; symbolIndex + 1 < encodedSymbolIds_.size(); ++symbolIndex)
      adjacentPairMerges_.push_back(getPairMerge(
          pairKeyToMergedSymbolId_,
          encodedSymbolIds_[symbolIndex],
          encodedSymbolIds_[symbolIndex + 1]
      ));

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
        adjacentPairMerges_[symbolIndex - 1] = getPairMerge(
            pairKeyToMergedSymbolId_,
            encodedSymbolIds_[symbolIndex - 1],
            lowestRankMerge->mergedSymbolId
        );

      // next pair
      if (symbolIndex + 2 < encodedSymbolIds_.size())
        adjacentPairMerges_[symbolIndex + 1] = getPairMerge(
            pairKeyToMergedSymbolId_,
            lowestRankMerge->mergedSymbolId,
            encodedSymbolIds_[symbolIndex + 2]
        );

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

    if (symbol.size() > settings.word_prefix.size() + settings.word_suffix.size() &&
        symbol.starts_with(settings.word_prefix) &&
        symbol.ends_with(settings.word_suffix)) {
      std::string_view word = symbol;
      word.remove_prefix(settings.word_prefix.size());
      word.remove_suffix(settings.word_suffix.size());
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

  for (const std::string_view markupToken : settings.markup_tokens)
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

    if (std::holds_alternative<wdym::ByteFallback>(settings_.unknown_character_fallback))
      for (int byte = 0; byte < 256; ++byte)
        addToAlphabet(std::string(1, static_cast<char>(byte)));
    else if (const auto* token = std::get_if<std::string>(&settings_.unknown_character_fallback))
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
    BytePairEncodingSettings settings,
    StringKeySet symbols,
    unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId
)
    : TokenizerBase(settings.normalization),
      settings_(std::move(settings)),
      symbols_(std::move(symbols)),
      pairKeyToMergedSymbolId_(std::move(pairKeyToMergedSymbolId)),
      markupTokenToTokenId_(buildMarkupTokenToTokenId(settings_, symbols_)),
      symbolIdToTokenId_(buildSymbolIdToTokenId(symbols_)),
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
    if (entry.symbolIdCount == 0 ||
        entry.wordSize != word.size() ||
        !std::ranges::equal(std::span(entry.word).first(word.size()), word))
      continue;

    entry.isMostRecentlyUsed = true;
    set.entries[1 - way].isMostRecentlyUsed = false;

    return std::span(entry.symbolIds).first(entry.symbolIdCount);
  }

  return {};
}

void BytePairEncoding::WordCache::insert(
    std::string_view word,
    uint64_t hash,
    std::span<const uint32_t> symbolIds
) {
  static_assert(WAYS_PER_SET == 2);

  if (word.size() > MAX_WORD_SIZE || symbolIds.empty() || symbolIds.size() > MAX_SYMBOL_ID_COUNT)
    return;

  auto& set = getSet(hash);

  const auto& firstEntry = set.entries[0];
  const auto& secondEntry = set.entries[1];
  const size_t insertionWay = firstEntry.symbolIdCount == 0    ? 0
                              : secondEntry.symbolIdCount == 0 ? 1
                              : firstEntry.isMostRecentlyUsed  ? 1
                                                               : 0;

  auto& insertionEntry = set.entries[insertionWay];
  insertionEntry.wordSize = static_cast<uint8_t>(word.size());
  insertionEntry.symbolIdCount = static_cast<uint8_t>(symbolIds.size());
  insertionEntry.isMostRecentlyUsed = true;

  std::ranges::copy(word, insertionEntry.word.begin());
  std::ranges::copy(symbolIds, insertionEntry.symbolIds.begin());

  set.entries[1 - insertionWay].isMostRecentlyUsed = false;
}

BytePairEncoding BytePairEncoding::train_from_texts(TextStream texts, const TrainOptions& options) {
  const auto settings = getValidatedSettings(options.settings);

  BytePairEncodingTrainer trainer(settings);

  // MEMORY OPTIMIZATION: separate scope to free up wordToCount once done with it
  {
    auto wordToCount = getWordToCount(std::move(texts), settings);

    trainer.registerAlphabet(wordToCount);

    const auto minimumVocabularySize = trainer.symbols.size() + settings.markup_tokens.size();
    if (std::cmp_greater(minimumVocabularySize, options.max_vocabulary_size))
      throw std::invalid_argument(
          std::format(
              "max_vocabulary_size {} is too small: the corpus and markup tokens need at least {}",
              options.max_vocabulary_size,
              minimumVocabularySize
          )
      );

    trainer.buildInitialPairs(wordToCount);
  }

  while (
      std::cmp_less(
          trainer.symbols.size() + settings.markup_tokens.size(), options.max_vocabulary_size
      )
  ) {
    auto mostFrequentPairKey = trainer.popMostFrequentPair();
    if (!mostFrequentPairKey.has_value())
      break;

    trainer.mergePair(mostFrequentPairKey.value());
  }

  return BytePairEncoding(
      settings, std::move(trainer.symbols), std::move(trainer.pairKeyToMergedSymbolId)
  );
}

std::vector<uint32_t> BytePairEncoding::encode(std::string_view text) const {
  std::vector<uint32_t> ids;
  encode(text, ids);
  return ids;
}

void BytePairEncoding::encode(std::string_view text, std::vector<uint32_t>& ids) const {
  std::string normalizationBuffer;
  encodeWords(normalizer_.normalize(text, normalizationBuffer), ids, nullptr);
}

void BytePairEncoding::encode(
    std::string_view text,
    std::vector<uint32_t>& ids,
    WordCache& cache
) const {
  std::string normalizationBuffer;
  encodeWords(normalizer_.normalize(text, normalizationBuffer), ids, &cache);
}

std::vector<uint32_t> BytePairEncoding::encode_normalized(std::string_view text) const {
  std::vector<uint32_t> ids;
  encode_normalized(text, ids);
  return ids;
}

void BytePairEncoding::encode_normalized(std::string_view text, std::vector<uint32_t>& ids) const {
  encodeWords(text, ids, nullptr);
}

void BytePairEncoding::encode_normalized(
    std::string_view text,
    std::vector<uint32_t>& ids,
    WordCache& cache
) const {
  encodeWords(text, ids, &cache);
}

void BytePairEncoding::encodeWords(
    std::string_view text,
    std::vector<uint32_t>& ids,
    WordCache* cache
) const {
  BytePairEncodingWordEncoder wordEncoder(
      settings_, symbols_, pairKeyToMergedSymbolId_, symbolIdToTokenId_, findFallbackTokenId()
  );

  splitWords(text, settings_.word_delimiter, [&](std::string_view word) {
    const auto wordHash = wordToWholeWordSymbolId_.hash_for(word);

    if (const auto it = wordToWholeWordSymbolId_.find(word, wordHash);
        it != wordToWholeWordSymbolId_.end()) {
      ids.push_back(symbolIdToTokenId_[it->second]);
      return;
    }

    if (cache == nullptr) {
      wordEncoder.encode(word, ids);
      return;
    }

    if (const auto cachedIds = cache->find(word, wordHash.m_mixed_hash); !cachedIds.empty()) {
      ids.append_range(cachedIds);
      return;
    }

    const auto wordFirstIdIndex = ids.size();
    wordEncoder.encode(word, ids);
    cache->insert(word, wordHash.m_mixed_hash, std::span(ids).subspan(wordFirstIdIndex));
  });
}

std::vector<uint32_t> BytePairEncoding::encode_parts(
    std::span<const TextOrMarkupToken> parts
) const {
  std::vector<uint32_t> ids;
  encode_parts(parts, ids);
  return ids;
}

void BytePairEncoding::encode_parts(
    std::span<const TextOrMarkupToken> parts,
    std::vector<uint32_t>& ids
) const {
  encodeParts(parts, ids, nullptr);
}

void BytePairEncoding::encode_parts(
    std::span<const TextOrMarkupToken> parts,
    std::vector<uint32_t>& ids,
    WordCache& cache
) const {
  encodeParts(parts, ids, &cache);
}

void BytePairEncoding::encodeParts(
    std::span<const TextOrMarkupToken> parts,
    std::vector<uint32_t>& ids,
    WordCache* cache
) const {
  for (const TextOrMarkupToken& part : parts) {
    if (const auto* markupToken = std::get_if<MarkupToken>(&part)) {
      const auto tokenId = markup_token_to_id(markupToken->text);
      if (!tokenId.has_value())
        throw std::invalid_argument(
            std::format("{:?} is not one of the tokenizer's markup tokens", markupToken->text)
        );

      ids.push_back(*tokenId);
    } else if (cache != nullptr) {
      encode(std::get<std::string>(part), ids, *cache);
    } else {
      encode(std::get<std::string>(part), ids);
    }
  }
}

std::string BytePairEncoding::decode(
    std::span<const uint32_t> ids,
    const BytePairEncodingDecodeOptions& options
) const {
  std::string text;
  decode(ids, text, options);
  return text;
}

void BytePairEncoding::decode(
    std::span<const uint32_t> ids,
    std::string& text,
    const BytePairEncodingDecodeOptions& options
) const {
  for (const TextOrMarkupToken& part : decode_parts(ids, options)) {
    if (const auto* markupToken = std::get_if<MarkupToken>(&part))
      text.append(markupToken->text);
    else
      text.append(std::get<std::string>(part));
  }
}

std::vector<wdym::TextOrMarkupToken> BytePairEncoding::decode_parts(
    std::span<const uint32_t> ids,
    const BytePairEncodingDecodeOptions& options
) const {
  std::vector<TextOrMarkupToken> parts;
  std::string symbolText;

  const auto appendTextPart = [&](bool removeTrailingWordSuffix) {
    if (symbolText.empty())
      return;

    const bool removeLeadingWordPrefix = parts.empty() && options.remove_leading_word_prefix;
    std::string text = decodeSymbolText(
        symbolText,
        removeLeadingWordPrefix,
        removeTrailingWordSuffix,
        options.restore_word_delimiters
    );
    if (!text.empty())
      parts.emplace_back(std::move(text));

    symbolText.clear();
  };

  for (const uint32_t tokenId : ids) {
    const auto& token = tokenIdToTokenSource_.at(tokenId);

    if (token.isMarkupToken) {
      appendTextPart(false);
      parts.emplace_back(MarkupToken{markupTokenToTokenId_.values()[token.index].first});
    } else {
      symbolText.append(symbols_.values()[token.index]);
    }
  }

  appendTextPart(options.remove_trailing_word_suffix);
  return parts;
}

std::string BytePairEncoding::decodeSymbolText(
    std::string_view symbolText,
    bool removeLeadingWordPrefix,
    bool removeTrailingWordSuffix,
    bool restoreWordDelimiters
) const {
  if (removeLeadingWordPrefix && symbolText.starts_with(settings_.word_prefix))
    symbolText.remove_prefix(settings_.word_prefix.size());

  if (removeTrailingWordSuffix && symbolText.ends_with(settings_.word_suffix))
    symbolText.remove_suffix(settings_.word_suffix.size());

  std::string text;
  const std::string wordSuffixAndPrefix = settings_.word_suffix + settings_.word_prefix;

  if (restoreWordDelimiters && !wordSuffixAndPrefix.empty())
    text.append_range(
        symbolText |
        std::views::split(std::string_view(wordSuffixAndPrefix)) |
        std::views::join_with(std::string_view(settings_.word_delimiter))
    );
  else
    text = symbolText;

  if (const auto* byteFallback = std::get_if<ByteFallback>(&settings_.unknown_character_fallback)) {
    std::string replacedText;
    if (replaceInvalidUtf8(text, byteFallback->invalid_utf8_replacement, replacedText))
      text = std::move(replacedText);
  }

  return text;
}

std::optional<uint32_t> BytePairEncoding::token_to_id(std::string_view token) const {
  if (const auto symbolId = findSymbolId(symbols_, token))
    return symbolIdToTokenId_[*symbolId];

  return std::nullopt;
}

std::optional<uint32_t> BytePairEncoding::markup_token_to_id(std::string_view token) const {
  const auto it = markupTokenToTokenId_.find(token);
  if (it == markupTokenToTokenId_.end())
    return std::nullopt;

  return it->second;
}

// TODO: consider renaming to findFallbackTokenId
std::optional<uint32_t> BytePairEncoding::findFallbackTokenId() const {
  if (const auto* markupFallback = std::get_if<MarkupToken>(&settings_.unknown_character_fallback))
    return markup_token_to_id(markupFallback->text);

  if (const auto* stringFallback = std::get_if<std::string>(&settings_.unknown_character_fallback))
    return token_to_id(*stringFallback);

  return std::nullopt;
}

void BytePairEncoding::save(const std::filesystem::path& path) const {
  // TODO
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
