module;

#include <ankerl/unordered_dense.h>

module wdym;

import std.compat;
import :containers;

using wdym::BytePairEncoding;
using wdym::StringKeyMap;
using wdym::StringKeySet;
namespace unordered_dense = ankerl::unordered_dense;

namespace {
constexpr std::string_view WHITESPACE = " \t\r\n";
// U+FFFD
constexpr std::string_view REPLACEMENT_CHARACTER = "�";

// Really wanted this function to return a generator, which would be nicer
// to use at call sites. But generators can't be inlined (as opposed to this
// callback-form function) so there is a performance hit. To revisit...
template <std::invocable<std::string_view> OnWord>
void splitWords(std::string_view text, OnWord&& onWord) {
  size_t start = text.find_first_not_of(WHITESPACE);

  while (start != std::string_view::npos) {
    const auto end = text.find_first_of(WHITESPACE, start);
    // end could evaluate to std::string_view::npos, but string sizes are no
    // bigger than npos. Thus, the remaining word length text.size() - start is
    // covered by npos - start:
    // text.size() <= npos = static_cast<size_t>(-1) \implies
    // text.size() - start <= npos - start
    const auto word = text.substr(start, end - start);

    onWord(word);

    start = text.find_first_not_of(WHITESPACE, end);
  }
}

// OPTIMIZATION: make use of multithreading
StringKeyMap<uint64_t> getWordToCount(wdym::TextStream texts) {
  StringKeyMap<uint64_t> wordToCount;

  for (const auto text : texts) {
    splitWords(text, [&](std::string_view word) {
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
  const size_t unicodeCharacterLength = leadingByte >> 7 == 0b0       ? 1
                                        : leadingByte >> 5 == 0b110   ? 2
                                        : leadingByte >> 4 == 0b1110  ? 3
                                        : leadingByte >> 3 == 0b11110 ? 4
                                                                      : 0;

  if (unicodeCharacterLength == 0 || text.size() < unicodeCharacterLength)
    return {};

  // Validate that this is a valid unicode sequence
  for (size_t i = 1; i < unicodeCharacterLength; ++i)
    if (static_cast<unsigned char>(text[i]) >> 6 != 0b10)
      return {};

  return text.substr(0, unicodeCharacterLength);
}

std::generator<std::string_view> splitWordSymbols(
    std::string_view word,
    const wdym::BytePairEncodingSettings& settings,
    bool yieldUnadjoinedBoundaryCharacters = false
) {
  const bool hasPrefix = !settings.word_prefix.empty();
  const bool hasSuffix = !settings.word_suffix.empty();

  if (hasPrefix && !settings.adjoin_prefix)
    co_yield settings.word_prefix;

  bool isFirstCharacter = true;

  while (!word.empty()) {
    const auto tentativeCharacter = getNextCharacter(word);
    const auto character = tentativeCharacter.empty() ? REPLACEMENT_CHARACTER : tentativeCharacter;
    word.remove_prefix(std::max<size_t>(tentativeCharacter.size(), 1));

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

      co_yield adjoinedSymbol;

      // It'd feel more natural if the first character were yielded after the
      // prefix-adjoined one, and the last character before the suffix-adjoined
      // one. But our usage doesn't require this
      if (yieldUnadjoinedBoundaryCharacters)
        co_yield character;
    } else {
      co_yield character;
    }
  }

  if (hasSuffix && !settings.adjoin_suffix)
    co_yield settings.word_suffix;
}

class BytePairEncodingTrainer {
 public:
  explicit BytePairEncodingTrainer(const wdym::BytePairEncodingSettings& settings)
      : settings_(settings) {}

  void registerAlphabet(const StringKeyMap<uint64_t>& wordToCount) {
    // Instead of immediately generating symbol IDs for each character, we first
    // collect the characters into an ordered set and generate the IDs from that.
    // This gives characters the same IDs no matter the order of words, lines or
    // input files, and consequently yields the same merges.
    std::set<std::string> alphabet;
    for (const auto& [word, _] : wordToCount)
      for (const auto symbol : splitWordSymbols(word, settings_, true))
        alphabet.insert(std::string(symbol));

    for (const auto& symbol : alphabet)
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
      for (const auto symbol : splitWordSymbols(text, settings_))
        word.symbols.push_back(setAndGetSymbolId(symbol));

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

BytePairEncoding BytePairEncoding::train_from_texts(TextStream texts, const TrainOptions& options) {
  if (options.settings.use_byte_fallback && !options.settings.token_fallback.empty())
    throw std::invalid_argument(
        std::format(
            "use_byte_fallback is true and token_fallback is {:?}: only one fallback can be used",
            options.settings.token_fallback
        )
    );

  BytePairEncodingTrainer trainer(options.settings);

  // separate scope to free up wordToCount once done with it
  {
    auto wordToCount = getWordToCount(std::move(texts));

    trainer.registerAlphabet(wordToCount);

    if (std::cmp_greater(trainer.symbols.size(), options.max_vocabulary_size))
      throw std::invalid_argument(
          std::format(
              "max_vocabulary_size {} is too small: corpus needs at least {} symbols",
              options.max_vocabulary_size, trainer.symbols.size()
          )
      );

    trainer.buildInitialPairs(wordToCount);
  }

  while (std::cmp_less(trainer.symbols.size(), options.max_vocabulary_size)) {
    auto mostFrequentPairKey = trainer.popMostFrequentPair();
    if (!mostFrequentPairKey.has_value())
      break;

    trainer.mergePair(mostFrequentPairKey.value());
  }

  return BytePairEncoding(
      options.settings, std::move(trainer.symbols), std::move(trainer.pairKeyToMergedSymbolId)
  );
}

std::vector<uint32_t> BytePairEncoding::encode(std::string_view text) const {
  std::vector<uint32_t> finalSymbolIds;

  // OPTIMIZATION: each word can be treated separately, as long their order is retained
  splitWords(text, [&](std::string_view word) {
    std::vector<uint32_t> symbolIds;
    // TODO: optimize reservation since word could be filled with lots of
    // whitespace
    symbolIds.reserve(word.size());

    for (const auto symbol : splitWordSymbols(word, settings_)) {
      const auto it = symbols_.find(symbol);
      if (it != symbols_.end()) {
        symbolIds.push_back(static_cast<uint32_t>(it - symbols_.begin()));
      } else {
        // push unknown character?
      }
    }

    if (symbolIds.size() <= 1) {
      finalSymbolIds.append_range(symbolIds);
      return;
    }

    struct Merge {
      size_t rank;
      size_t leftSymbolIndex;
      uint32_t mergedSymbolId;
    };

    std::optional<Merge> merge = std::nullopt;
    do {
      merge = std::nullopt;

      for (size_t symbolIndex = 0; symbolIndex + 1 < symbolIds.size(); ++symbolIndex) {
        auto leftSymbolId = symbolIds[symbolIndex];
        auto rightSymbolId = symbolIds[symbolIndex + 1];

        auto pairKey = getPairKey(leftSymbolId, rightSymbolId);
        auto it = pairKeyToMergedSymbolId_.find(pairKey);
        if (it == pairKeyToMergedSymbolId_.end())
          continue;

        auto pairRank = static_cast<size_t>(it - pairKeyToMergedSymbolId_.begin());
        if (!merge.has_value() || pairRank < merge->rank)
          merge =
              Merge{.rank = pairRank, .leftSymbolIndex = symbolIndex, .mergedSymbolId = it->second};
      }

      if (merge.has_value()) {
        symbolIds[merge->leftSymbolIndex] = merge->mergedSymbolId;
        symbolIds.erase(symbolIds.begin() + merge->leftSymbolIndex + 1);
      }
    } while (merge.has_value());

    finalSymbolIds.append_range(symbolIds);
  });

  return finalSymbolIds;
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
