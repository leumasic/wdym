module;

#include <ankerl/unordered_dense.h>

module wdym;

import std.compat;
import :containers;
import :normalization;

using wdym::BytePairEncoding;
using wdym::FrequentPairMerges;
using wdym::PairMerge;
using wdym::StringKeyMap;
using wdym::StringKeySet;
namespace unordered_dense = ankerl::unordered_dense;

namespace {
constexpr uint32_t NO_MERGE_RANK = std::numeric_limits<uint32_t>::max();
constexpr uint32_t NO_SYMBOL_ID = std::numeric_limits<uint32_t>::max();
constexpr uint16_t NO_SLOT = std::numeric_limits<uint16_t>::max();

std::atomic<uint64_t> nextInstanceId = 1;

// Really wanted this function to return a generator, which would be nicer
// to use at call sites. But generators can't be inlined (as opposed to this
// callback-form function) so there is a performance hit. To revisit...
template <std::invocable<std::string_view> OnWord>
void splitWords(std::string_view text, std::string_view delimiter, OnWord&& onWord) {
  if (delimiter.empty()) {
    if (!text.empty())
      onWord(text);

    return;
  }

  size_t start = 0;

  while (true) {
    const auto end = text.find(delimiter, start);
    // When there's no delimiter ahead, end evaluates to
    // std::string_view::npos and we'd want the expression end - start below to
    // cover the remainder of the string. And it does!
    // The remaining word length text.size() - start is covered by npos - start:
    // text.size() <= npos = static_cast<size_t>(-1) \implies
    // text.size() - start <= npos - start
    const auto word = text.substr(start, end - start);

    if (!word.empty())
      onWord(word);

    if (end == std::string_view::npos)
      return;

    start = end + delimiter.size();
  }
}

// OPTIMIZATION: make use of multithreading
StringKeyMap<uint64_t> getWordToCount(
    wdym::TextStream texts,
    const wdym::BytePairEncodingSettings& settings
) {
  StringKeyMap<uint64_t> wordToCount;
  std::string normalizationBuffer;

  for (const auto text : texts) {
    const auto normalizedText = wdym::normalize(text, settings.normalization, normalizationBuffer);

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
  const auto leadingByte = static_cast<unsigned char>(text[0]);
  return text.substr(0, std::max(1, std::countl_one(leadingByte)));
}

template <std::invocable<std::string_view> OnCharacter>
void splitCharacters(std::string_view text, OnCharacter&& onCharacter) {
  while (!text.empty()) {
    const auto character = getNextCharacter(text);
    text.remove_prefix(character.size());

    onCharacter(character);
  }
}

std::string_view removeWordPrefixAndSuffix(
    std::string_view word,
    const wdym::BytePairEncodingSettings& settings
) {
  if (word.starts_with(settings.word_prefix))
    word.remove_prefix(settings.word_prefix.size());

  if (word.ends_with(settings.word_suffix))
    word.remove_suffix(settings.word_suffix.size());

  return word;
}

template <std::invocable<std::string_view> OnSymbol>
void splitWordSymbols(
    std::string_view word,
    const wdym::BytePairEncodingSettings& settings,
    OnSymbol&& onSymbol
) {
  const bool hasPrefix = !settings.word_prefix.empty();
  const bool hasSuffix = !settings.word_suffix.empty();

  word = removeWordPrefixAndSuffix(word, settings);
  const bool hasCharacters = !word.empty();

  if (hasPrefix && (!settings.adjoin_prefix || !hasCharacters))
    onSymbol(settings.word_prefix);

  bool isFirstCharacter = true;
  std::string adjoinedSymbol;

  while (!word.empty()) {
    const auto character = getNextCharacter(word);
    word.remove_prefix(character.size());

    const bool hasAdjoinedPrefix = isFirstCharacter && hasPrefix && settings.adjoin_prefix;
    const bool hasAdjoinedSuffix = word.empty() && hasSuffix && settings.adjoin_suffix;
    isFirstCharacter = false;

    if (hasAdjoinedPrefix || hasAdjoinedSuffix) {
      adjoinedSymbol.clear();

      if (hasAdjoinedPrefix)
        adjoinedSymbol.append(settings.word_prefix);

      adjoinedSymbol.append(character);

      if (hasAdjoinedSuffix)
        adjoinedSymbol.append(settings.word_suffix);

      onSymbol(adjoinedSymbol);
    } else {
      onSymbol(character);
    }
  }

  if (hasSuffix && (!settings.adjoin_suffix || !hasCharacters))
    onSymbol(settings.word_suffix);
}

std::optional<uint32_t> findSymbolId(const StringKeySet& symbols, std::string_view symbol) {
  const auto it = symbols.find(symbol);
  if (it == symbols.end())
    return std::nullopt;

  return static_cast<uint32_t>(it - symbols.begin());
}

std::optional<uint32_t> findSymbolId(
    const StringKeySet& symbols,
    const std::array<uint32_t, 128>& asciiCharacterToSymbolId,
    std::string_view symbol
) {
  if (symbol.size() != 1 ||
      static_cast<unsigned char>(symbol[0]) >= asciiCharacterToSymbolId.size())
    return findSymbolId(symbols, symbol);

  const auto symbolId = asciiCharacterToSymbolId[static_cast<unsigned char>(symbol[0])];
  if (symbolId == NO_SYMBOL_ID)
    return std::nullopt;

  return symbolId;
}

std::array<uint32_t, 128> findAsciiCharacterToSymbolId(const StringKeySet& symbols) {
  std::array<uint32_t, 128> asciiCharacterToSymbolId;

  for (size_t byte = 0; byte < asciiCharacterToSymbolId.size(); ++byte) {
    const auto character = static_cast<char>(byte);
    asciiCharacterToSymbolId[byte] =
        findSymbolId(symbols, std::string_view(&character, 1)).value_or(NO_SYMBOL_ID);
  }

  return asciiCharacterToSymbolId;
}

PairMerge findPairMerge(
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

PairMerge findPairMerge(
    const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
    const FrequentPairMerges& frequentPairMerges,
    uint32_t leftSymbolId,
    uint32_t rightSymbolId
) {
  const auto& symbolIdToSlot = frequentPairMerges.symbolIdToSlot;

  if (leftSymbolId < symbolIdToSlot.size() && rightSymbolId < symbolIdToSlot.size()) {
    const auto leftSlot = symbolIdToSlot[leftSymbolId];
    const auto rightSlot = symbolIdToSlot[rightSymbolId];

    if (leftSlot != NO_SLOT && rightSlot != NO_SLOT)
      return frequentPairMerges
          .slotPairToMerge[leftSlot * FrequentPairMerges::SLOT_COUNT + rightSlot];
  }

  return findPairMerge(pairKeyToMergedSymbolId, leftSymbolId, rightSymbolId);
}

void mergeSymbolIds(
    std::vector<uint32_t>& symbolIds,
    const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
    const FrequentPairMerges& frequentPairMerges,
    std::vector<PairMerge>& pairMerges
) {
  pairMerges.clear();
  for (size_t symbolIndex = 0; symbolIndex + 1 < symbolIds.size(); ++symbolIndex)
    pairMerges.push_back(findPairMerge(
        pairKeyToMergedSymbolId, frequentPairMerges, symbolIds[symbolIndex],
        symbolIds[symbolIndex + 1]
    ));

  while (true) {
    const auto lowestRankMerge = std::ranges::min_element(pairMerges, {}, &PairMerge::rank);
    if (lowestRankMerge == pairMerges.end() || lowestRankMerge->rank == NO_MERGE_RANK)
      return;

    const auto symbolIndex = static_cast<size_t>(lowestRankMerge - pairMerges.begin());
    symbolIds[symbolIndex] = lowestRankMerge->mergedSymbolId;
    symbolIds.erase(symbolIds.begin() + symbolIndex + 1);
    pairMerges.erase(lowestRankMerge);

    if (symbolIndex < pairMerges.size())
      pairMerges[symbolIndex] = findPairMerge(
          pairKeyToMergedSymbolId, frequentPairMerges, symbolIds[symbolIndex],
          symbolIds[symbolIndex + 1]
      );

    if (symbolIndex > 0)
      pairMerges[symbolIndex - 1] = findPairMerge(
          pairKeyToMergedSymbolId, frequentPairMerges, symbolIds[symbolIndex - 1],
          symbolIds[symbolIndex]
      );
  }
}

wdym::BytePairEncodingSettings getPreparedSettings(wdym::BytePairEncodingSettings settings) {
  wdym::validateNormalizationSettings(settings.normalization);

  if (settings.use_byte_fallback && !settings.token_fallback.empty())
    throw std::invalid_argument(
        std::format(
            "use_byte_fallback is true and token_fallback is {:?}: only one fallback can be used",
            settings.token_fallback
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

class BytePairEncodingWordEncoder {
 public:
  BytePairEncodingWordEncoder(
      const wdym::BytePairEncodingSettings& settings,
      const StringKeySet& symbols,
      const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
      const FrequentPairMerges& frequentPairMerges,
      const std::array<uint32_t, 128>& asciiCharacterToSymbolId,
      std::optional<uint32_t> tokenFallbackId
  )
      : settings_(settings),
        symbols_(symbols),
        pairKeyToMergedSymbolId_(pairKeyToMergedSymbolId),
        frequentPairMerges_(frequentPairMerges),
        asciiCharacterToSymbolId_(asciiCharacterToSymbolId),
        tokenFallbackId_(tokenFallbackId),
        prefixSymbolId_(findSymbolId(symbols, settings.word_prefix)),
        suffixSymbolId_(findSymbolId(symbols, settings.word_suffix)) {}

  void encode(std::string_view word, std::vector<uint32_t>& ids) {
    symbolIds_.clear();
    splitWordSymbols(word, settings_, [this](std::string_view symbol) {
      appendWordSymbolIds(symbol);
    });
    mergeSymbolIds(symbolIds_, pairKeyToMergedSymbolId_, frequentPairMerges_, pairMerges_);
    ids.append_range(symbolIds_);
  }

 private:
  void appendWordSymbolIds(std::string_view symbol) {
    if (prefixSymbolId_.has_value() && symbol == settings_.word_prefix) {
      symbolIds_.push_back(*prefixSymbolId_);
      return;
    }

    if (suffixSymbolId_.has_value() && symbol == settings_.word_suffix) {
      symbolIds_.push_back(*suffixSymbolId_);
      return;
    }

    if (const auto symbolId = findSymbolId(symbols_, asciiCharacterToSymbolId_, symbol)) {
      symbolIds_.push_back(*symbolId);
      return;
    }

    const std::string_view prefix = settings_.word_prefix;
    const std::string_view suffix = settings_.word_suffix;

    const bool hasAdjoinedPrefix = settings_.adjoin_prefix && !prefix.empty() &&
                                   symbol.size() > prefix.size() && symbol.starts_with(prefix);
    if (hasAdjoinedPrefix) {
      appendSymbolOrFallbackIds(prefix);
      symbol.remove_prefix(prefix.size());
    }

    const bool hasAdjoinedSuffix = settings_.adjoin_suffix && !suffix.empty() &&
                                   symbol.size() > suffix.size() && symbol.ends_with(suffix);
    if (hasAdjoinedSuffix)
      symbol.remove_suffix(suffix.size());

    appendSymbolOrFallbackIds(symbol);

    if (hasAdjoinedSuffix)
      appendSymbolOrFallbackIds(suffix);
  }

  void appendSymbolOrFallbackIds(std::string_view symbol) {
    if (const auto symbolId = findSymbolId(symbols_, asciiCharacterToSymbolId_, symbol))
      symbolIds_.push_back(*symbolId);
    else if (settings_.use_byte_fallback)
      for (const char byte : symbol)
        symbolIds_.push_back(*findSymbolId(symbols_, std::string_view(&byte, 1)));
    else if (tokenFallbackId_.has_value())
      symbolIds_.push_back(*tokenFallbackId_);
  }

  const wdym::BytePairEncodingSettings& settings_;
  const StringKeySet& symbols_;
  const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId_;
  const FrequentPairMerges& frequentPairMerges_;
  const std::array<uint32_t, 128>& asciiCharacterToSymbolId_;
  const std::optional<uint32_t> tokenFallbackId_;
  const std::optional<uint32_t> prefixSymbolId_;
  const std::optional<uint32_t> suffixSymbolId_;

  std::vector<uint32_t> symbolIds_;
  std::vector<PairMerge> pairMerges_;
};

FrequentPairMerges findFrequentPairMerges(
    const wdym::BytePairEncodingSettings& settings,
    const StringKeySet& symbols,
    const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
    const std::array<uint32_t, 128>& asciiCharacterToSymbolId
) {
  FrequentPairMerges frequentPairMerges{
      .symbolIdToSlot = std::vector<uint16_t>(symbols.size(), NO_SLOT),
      .slotPairToMerge = {},
  };
  std::vector<uint32_t> slotToSymbolId;

  const auto addSymbol = [&](uint32_t symbolId) {
    if (slotToSymbolId.size() == FrequentPairMerges::SLOT_COUNT ||
        frequentPairMerges.symbolIdToSlot[symbolId] != NO_SLOT)
      return;

    frequentPairMerges.symbolIdToSlot[symbolId] = static_cast<uint16_t>(slotToSymbolId.size());
    slotToSymbolId.push_back(symbolId);
  };

  for (const auto symbolId : asciiCharacterToSymbolId)
    if (symbolId != NO_SYMBOL_ID)
      addSymbol(symbolId);

  for (const std::string_view boundary : {settings.word_prefix, settings.word_suffix})
    if (const auto symbolId = findSymbolId(symbols, boundary))
      addSymbol(*symbolId);

  for (const auto& [_, mergedSymbolId] : pairKeyToMergedSymbolId)
    addSymbol(mergedSymbolId);

  frequentPairMerges.slotPairToMerge.resize(
      FrequentPairMerges::SLOT_COUNT * FrequentPairMerges::SLOT_COUNT,
      PairMerge{.rank = NO_MERGE_RANK, .mergedSymbolId = 0}
  );

  for (size_t leftSlot = 0; leftSlot < slotToSymbolId.size(); ++leftSlot)
    for (size_t rightSlot = 0; rightSlot < slotToSymbolId.size(); ++rightSlot)
      frequentPairMerges.slotPairToMerge[leftSlot * FrequentPairMerges::SLOT_COUNT + rightSlot] =
          findPairMerge(
              pairKeyToMergedSymbolId, slotToSymbolId[leftSlot], slotToSymbolId[rightSlot]
          );

  return frequentPairMerges;
}

StringKeyMap<uint32_t> findWholeWordToSymbolId(
    const wdym::BytePairEncodingSettings& settings,
    const StringKeySet& symbols,
    const unordered_dense::map<uint64_t, uint32_t>& pairKeyToMergedSymbolId,
    const FrequentPairMerges& frequentPairMerges,
    const std::array<uint32_t, 128>& asciiCharacterToSymbolId,
    std::optional<uint32_t> tokenFallbackId
) {
  BytePairEncodingWordEncoder wordEncoder(
      settings, symbols, pairKeyToMergedSymbolId, frequentPairMerges, asciiCharacterToSymbolId,
      tokenFallbackId
  );
  StringKeyMap<uint32_t> wholeWordToSymbolId;
  std::vector<uint32_t> ids;

  for (uint32_t symbolId = 0; symbolId < symbols.size(); ++symbolId) {
    const std::string_view symbol = symbols.values()[symbolId];
    if (symbol.size() < settings.word_prefix.size() + settings.word_suffix.size() ||
        !symbol.starts_with(settings.word_prefix) || !symbol.ends_with(settings.word_suffix))
      continue;

    ids.clear();
    wordEncoder.encode(symbol, ids);

    if (ids.size() == 1 && ids.front() == symbolId)
      wholeWordToSymbolId.emplace(removeWordPrefixAndSuffix(symbol, settings), symbolId);
  }

  return wholeWordToSymbolId;
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

    if (settings_.use_byte_fallback)
      for (int byte = 0; byte < 256; ++byte)
        addToAlphabet(std::string(1, static_cast<char>(byte)));

    if (!settings_.token_fallback.empty() &&
        !std::ranges::contains(settings_.markup_tokens, settings_.token_fallback))
      addToAlphabet(settings_.token_fallback);

    for (const auto& [word, _] : wordToCount) {
      splitWordSymbols(word, settings_, addToAlphabet);
      splitCharacters(word, addToAlphabet);
    }

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

      // This is only an upper bound on the total number of unicode characters, which occurs when
      // all characters are single-byte unicode characters. We add all characters as symbols, plus
      // up to two for a separate prefix and suffix
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

    const auto replacePair = [&](uint64_t oldPairKey, uint64_t newPairKey, uint64_t parentWordCount,
                                 uint32_t wordIndex) {
      auto& oldPairCount = pairToCount_.at(oldPairKey);
      oldPairCount -= parentWordCount;

      // Memory optimization. Training works without it
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
    : settings_(std::move(settings)),
      symbols_(std::move(symbols)),
      pairKeyToMergedSymbolId_(std::move(pairKeyToMergedSymbolId)),
      asciiCharacterToSymbolId_(findAsciiCharacterToSymbolId(symbols_)),
      frequentPairMerges_(findFrequentPairMerges(
          settings_,
          symbols_,
          pairKeyToMergedSymbolId_,
          asciiCharacterToSymbolId_
      )),
      wholeWordToSymbolId_(findWholeWordToSymbolId(
          settings_,
          symbols_,
          pairKeyToMergedSymbolId_,
          frequentPairMerges_,
          asciiCharacterToSymbolId_,
          token_to_id(settings_.token_fallback)
      )),
      instanceId_(nextInstanceId++) {}

BytePairEncoding::Cache::Cache(size_t capacity)
    : entries_(2 * std::bit_ceil(std::max<size_t>(capacity / 2, 1))) {}

void BytePairEncoding::Cache::clear() {
  std::ranges::fill(entries_, Entry{});
}

std::span<const uint32_t> BytePairEncoding::Cache::find(std::string_view word, uint64_t hash) {
  if (word.size() > MAX_WORD_SIZE)
    return {};

  const auto firstEntryIndex = (hash & (entries_.size() / 2 - 1)) * 2;

  for (size_t way = 0; way < 2; ++way) {
    auto& entry = entries_[firstEntryIndex + way];
    if (entry.idCount == 0 || entry.wordSize != word.size() ||
        !std::ranges::equal(std::span(entry.word).first(word.size()), word))
      continue;

    entry.isRecent = true;
    entries_[firstEntryIndex + (way ^ 1)].isRecent = false;
    return std::span(entry.ids).first(entry.idCount);
  }

  return {};
}

void BytePairEncoding::Cache::insert(
    std::string_view word,
    uint64_t hash,
    std::span<const uint32_t> ids
) {
  if (word.size() > MAX_WORD_SIZE || ids.empty() || ids.size() > MAX_ID_COUNT)
    return;

  const auto firstEntryIndex = (hash & (entries_.size() / 2 - 1)) * 2;
  const auto& firstEntry = entries_[firstEntryIndex];
  const auto& secondEntry = entries_[firstEntryIndex + 1];
  const size_t way = firstEntry.idCount == 0    ? 0
                     : secondEntry.idCount == 0 ? 1
                     : firstEntry.isRecent      ? 1
                                                : 0;

  auto& entry = entries_[firstEntryIndex + way];
  entry.wordSize = static_cast<uint8_t>(word.size());
  entry.idCount = static_cast<uint8_t>(ids.size());
  entry.isRecent = true;
  std::ranges::copy(word, entry.word.begin());
  std::ranges::copy(ids, entry.ids.begin());

  entries_[firstEntryIndex + (way ^ 1)].isRecent = false;
}

BytePairEncoding BytePairEncoding::train_from_texts(TextStream texts, const TrainOptions& options) {
  const auto settings = getPreparedSettings(options.settings);

  BytePairEncodingTrainer trainer(settings);

  // separate scope to free up wordToCount once done with it
  {
    auto wordToCount = getWordToCount(std::move(texts), settings);

    trainer.registerAlphabet(wordToCount);

    const auto minimumVocabularySize = trainer.symbols.size() + settings.markup_tokens.size();
    if (std::cmp_greater(minimumVocabularySize, options.max_vocabulary_size))
      throw std::invalid_argument(
          std::format(
              "max_vocabulary_size {} is too small: the corpus and markup tokens need at least {}",
              options.max_vocabulary_size, minimumVocabularySize
          )
      );

    trainer.buildInitialPairs(wordToCount);
  }

  while (std::cmp_less(
      trainer.symbols.size() + settings.markup_tokens.size(), options.max_vocabulary_size
  )) {
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
  encodeWords(wdym::normalize(text, settings_.normalization, normalizationBuffer), ids, nullptr);
}

void BytePairEncoding::encode(
    std::string_view text,
    std::vector<uint32_t>& ids,
    Cache& cache
) const {
  std::string normalizationBuffer;
  encodeWords(wdym::normalize(text, settings_.normalization, normalizationBuffer), ids, &cache);
}

std::string BytePairEncoding::normalize(std::string_view text) const {
  std::string normalizationBuffer;
  return std::string(wdym::normalize(text, settings_.normalization, normalizationBuffer));
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
    Cache& cache
) const {
  encodeWords(text, ids, &cache);
}

void BytePairEncoding::encodeWords(
    std::string_view text,
    std::vector<uint32_t>& ids,
    Cache* cache
) const {
  if (cache != nullptr && cache->tokenizerId_ != instanceId_) {
    cache->clear();
    cache->tokenizerId_ = instanceId_;
  }

  BytePairEncodingWordEncoder wordEncoder(
      settings_, symbols_, pairKeyToMergedSymbolId_, frequentPairMerges_, asciiCharacterToSymbolId_,
      token_to_id(settings_.token_fallback)
  );

  splitWords(text, settings_.word_delimiter, [&](std::string_view word) {
    const auto wordWithoutPrefixAndSuffix = removeWordPrefixAndSuffix(word, settings_);
    const auto hash = wholeWordToSymbolId_.hash_for(wordWithoutPrefixAndSuffix);

    if (const auto it = wholeWordToSymbolId_.find(wordWithoutPrefixAndSuffix, hash);
        it != wholeWordToSymbolId_.end()) {
      ids.push_back(it->second);
      return;
    }

    if (cache == nullptr) {
      wordEncoder.encode(word, ids);
      return;
    }

    if (const auto cachedIds = cache->find(wordWithoutPrefixAndSuffix, hash.m_mixed_hash);
        !cachedIds.empty()) {
      ids.append_range(cachedIds);
      return;
    }

    const auto firstIdIndex = ids.size();
    wordEncoder.encode(word, ids);
    cache->insert(
        wordWithoutPrefixAndSuffix, hash.m_mixed_hash, std::span(ids).subspan(firstIdIndex)
    );
  });
}

std::optional<uint32_t> BytePairEncoding::token_to_id(std::string_view token) const {
  if (const auto it = std::ranges::find(settings_.markup_tokens, token);
      it != settings_.markup_tokens.end())
    return static_cast<uint32_t>(symbols_.size() + (it - settings_.markup_tokens.begin()));

  return findSymbolId(symbols_, token);
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
