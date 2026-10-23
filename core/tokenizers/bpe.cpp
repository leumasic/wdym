module wdym;

import std.compat;

namespace {
constexpr std::string_view WHITESPACE = " \t\r\n";
constexpr std::string_view END_OF_WORD = "</w>";

struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

using WordToCount = std::unordered_map<std::string, uint64_t, StringHash, std::equal_to<>>;

WordToCount getWordToCount(wdym::TextStream texts) {
  WordToCount wordToCount;

  for (const auto text : texts) {
    size_t start = text.find_first_not_of(WHITESPACE);

    while (start != std::string_view::npos) {
      const auto end = text.find_first_of(WHITESPACE, start);
      const auto word = text.substr(start, end - start);

      if (auto it = wordToCount.find(word); it != wordToCount.end())
        ++it->second;
      else
        wordToCount.emplace(word, 1);

      start = text.find_first_not_of(WHITESPACE, end);
    }
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

uint64_t getPairKey(uint32_t left, uint32_t right) {
  return (uint64_t{left} << 32) | right;
}

template <typename T, typename U>
  requires std::same_as<std::remove_cvref_t<U>, T>
void pushBackIfEmptyOrNotLast(std::vector<T>& elements, U&& value) {
  if (elements.empty() || elements.back() != value)
    elements.push_back(std::forward<U>(value));
}

// �
// 1110 1111 (0xEF)
// 1011 1111 (0xBF)
// 1011 1101 (0xBD)
constexpr std::string_view REPLACEMENT_CHARACTER = "\xEF\xBF\xBD";

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

std::generator<std::string_view> splitCharacters(std::string_view text) {
  while (!text.empty()) {
    const auto nextCharacter = getNextCharacter(text);
    co_yield nextCharacter.empty() ? REPLACEMENT_CHARACTER : nextCharacter;
    text.remove_prefix(std::max<size_t>(nextCharacter.size(), 1));
  }
}
}  // namespace

using wdym::BytePairEncoding;

BytePairEncoding::BytePairEncoding() {}

BytePairEncoding BytePairEncoding::train_from_texts(TextStream texts, const TrainOptions& options) {
  auto wordToCount = getWordToCount(std::move(texts));

  BytePairEncoding tokenizer;
  std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>> symbolToId;

  const auto setAndGetIdOfSymbol = [&](const std::string_view symbol) {
    if (auto it = symbolToId.find(symbol); it != symbolToId.end())
      return static_cast<uint32_t>(it->second);

    const auto id = static_cast<uint32_t>(tokenizer.idToSymbol.size());

    tokenizer.idToSymbol.emplace_back(symbol);
    symbolToId.emplace(symbol, id);

    return id;
  };

  // This is a necessary (but not sufficient) step to guarantee that corpora
  // yield identical tokenization rules no matter the order of words or lines
  // or inputted files
  std::set<std::string_view> alphabet;
  for (const auto& [text, _] : wordToCount)
    for (const auto character : splitCharacters(text))
      alphabet.insert(character);

  for (const auto character : alphabet)
    setAndGetIdOfSymbol(character);
  setAndGetIdOfSymbol(END_OF_WORD);

  // Populate word list
  std::vector<Word> words;
  words.reserve(wordToCount.size());
  for (const auto& [text, count] : wordToCount) {
    auto& word = words.emplace_back(Word{.symbols = {}, .count = count});

    // This is only an upper bound on the total number of unicode characters, which occurs when all
    // characters are single-byte unicode characters.
    // We add all characters as symbols, including eow symbol
    word.symbols.reserve(text.size() + 1);
    for (const auto character : splitCharacters(text))
      word.symbols.push_back(setAndGetIdOfSymbol(character));
    word.symbols.push_back(setAndGetIdOfSymbol(END_OF_WORD));

    // NOTE: an optimization would be to populate pairToCount and pairToWordIndices here instead of
    // in another loop below
  }

  // Populate initial pairs
  std::unordered_map<uint64_t, uint64_t> pairToCount;
  std::unordered_map<uint64_t, std::vector<uint32_t>> pairToWordIndices;
  for (const auto& [wordIndex, word] : std::views::enumerate(words)) {
    for (size_t symbolIndex = 0; symbolIndex + 1 < word.symbols.size(); ++symbolIndex) {
      const auto key = getPairKey(word.symbols[symbolIndex], word.symbols[symbolIndex + 1]);
      pairToCount[key] += word.count;

      pushBackIfEmptyOrNotLast(pairToWordIndices[key], static_cast<uint32_t>(wordIndex));
    }
  }

  std::priority_queue<QueuedPair> pairCountPriorityQueue;
  for (const auto& [pairKey, count] : pairToCount)
    pairCountPriorityQueue.emplace(QueuedPair{.pairKey = pairKey, .count = count});

  // Perform merges
  while (!pairCountPriorityQueue.empty() &&
         std::cmp_less(tokenizer.idToSymbol.size(), options.vocab_size)) {
    // 1. get pair with the highest count
    auto queuedPair = pairCountPriorityQueue.top();
    pairCountPriorityQueue.pop();
    if (auto it = pairToCount.find(queuedPair.pairKey);
        it == pairToCount.end() || it->second != queuedPair.count)
      continue;

    // 2. create a symbol from the pair
    auto pairKey = queuedPair.pairKey;

    auto leftSymbolId = static_cast<uint32_t>(pairKey >> 32);
    auto rightSymbolId = static_cast<uint32_t>(pairKey);

    auto leftSymbol = tokenizer.idToSymbol[leftSymbolId];
    auto rightSymbol = tokenizer.idToSymbol[rightSymbolId];

    auto pairedSymbol = leftSymbol + rightSymbol;
    auto pairedSymbolId = setAndGetIdOfSymbol(pairedSymbol);

    std::unordered_set<uint64_t> pairsWithUpdatedCounts;

    // 3. update pairToWordIndices and pairToCount. For all words,
    // 3.1 remove symbols composing pair from symbol list
    for (const auto wordIndex : pairToWordIndices[pairKey]) {
      auto& word = words[wordIndex];

      for (size_t symbolIndex = 0; symbolIndex + 1 < word.symbols.size(); ++symbolIndex) {
        if (word.symbols[symbolIndex] == leftSymbolId &&
            word.symbols[symbolIndex + 1] == rightSymbolId) {
          // 3.2 cleanup previous and next pair that share merged symbols
          // Also form new pairs

          // previous pair
          if (symbolIndex >= 1) {
            auto oldPreviousPairKey =
                getPairKey(word.symbols[symbolIndex - 1], word.symbols[symbolIndex]);
            auto& previousPairCount = pairToCount.at(oldPreviousPairKey);
            previousPairCount -= word.count;
            if (previousPairCount == 0) {
              pairToCount.erase(oldPreviousPairKey);
            } else
              pairsWithUpdatedCounts.insert(oldPreviousPairKey);

            auto newPreviousPairKey = getPairKey(word.symbols[symbolIndex - 1], pairedSymbolId);
            pairToCount[newPreviousPairKey] += word.count;
            pairsWithUpdatedCounts.insert(newPreviousPairKey);

            pushBackIfEmptyOrNotLast(pairToWordIndices[newPreviousPairKey], wordIndex);
          }
          // next pair
          if (symbolIndex + 2 < word.symbols.size()) {
            auto oldNextPairKey =
                getPairKey(word.symbols[symbolIndex + 1], word.symbols[symbolIndex + 2]);
            auto& nextPairCount = pairToCount.at(oldNextPairKey);
            nextPairCount -= word.count;
            if (nextPairCount == 0)
              pairToCount.erase(oldNextPairKey);
            else
              pairsWithUpdatedCounts.insert(oldNextPairKey);

            auto newNextPairKey = getPairKey(pairedSymbolId, word.symbols[symbolIndex + 2]);
            pairToCount[newNextPairKey] += word.count;
            pairsWithUpdatedCounts.insert(newNextPairKey);

            pushBackIfEmptyOrNotLast(pairToWordIndices[newNextPairKey], wordIndex);
          }

          // 3.3 Overwrite the symbol and erase the next subsumed one
          word.symbols[symbolIndex] = pairedSymbolId;
          word.symbols.erase(word.symbols.begin() + symbolIndex + 1);
        }
      }
    }

    // 3.4 remove merged pair
    pairToCount.erase(pairKey);
    pairToWordIndices.erase(pairKey);

    // 3.5 add merge
    tokenizer.merges.emplace_back(leftSymbolId, rightSymbolId);

    // 3.6 add QueuedPair for each pair that has an updated count
    for (const auto pairKeyWithUpdatedCount : pairsWithUpdatedCounts)
      if (auto it = pairToCount.find(pairKeyWithUpdatedCount); it != pairToCount.end())
        pairCountPriorityQueue.emplace(
            QueuedPair{.pairKey = pairKeyWithUpdatedCount, .count = it->second}
        );
  }

  return tokenizer;
}

void BytePairEncoding::save(const std::filesystem::path& path) const {
  // TODO
}
