module;

#include <ankerl/unordered_dense.h>

export module wdym:bpe;

import std.compat;
import :containers;
import :markup_token;
import :normalization;
import :tokenizer;

namespace wdym {
struct TokenSource {
  bool isMarkupToken = false;
  uint32_t index = 0;
};
}  // namespace wdym

export namespace wdym {
struct ByteFallback {
  bool operator==(const ByteFallback&) const = default;
};

struct BytePairEncodingSettings {
  std::vector<std::string> markupTokens = {};

  std::string wordDelimiter = " ";

  // Prepended or adjoined to the start of each word
  std::string wordPrefix = "▁";
  // Appended or adjoined to the end of each word
  std::string wordSuffix = "";
  // Whether to adjoin the prefix to the first symbol of each word. If false,
  // it is prepended as a separate symbol
  bool adjoinPrefix = false;
  // Whether to adjoin the suffix to the last symbol of each word. If false, it
  // is appended as a separate symbol
  bool adjoinSuffix = true;

  std::variant<std::monostate, ByteFallback, std::string, MarkupToken> unknownCharacterFallback =
      {};

  bool operator==(const BytePairEncodingSettings&) const = default;
};

struct BytePairEncodingTrainOptions {
  int maxVocabularySize = 8000;
};

using BytePairEncodingEncodeOptions = std::monostate;

struct BytePairEncodingDecodeOptions {
  bool removeLeadingWordPrefix = true;
  bool removeTrailingWordSuffix = true;
  // Replaces word suffix followed by word prefix, with word delimiter
  bool restoreWordDelimiters = true;
  std::string invalidUtf8Replacement = "�";

  bool operator==(const BytePairEncodingDecodeOptions&) const = default;
};

class BytePairEncoding : public TokenizerBase<
                             BytePairEncoding,
                             BytePairEncodingTrainOptions,
                             BytePairEncodingSettings,
                             BytePairEncodingEncodeOptions,
                             BytePairEncodingDecodeOptions> {
  friend TokenizerBase;

 public:
  // Fixed-size cache to speed up encoding by storing token IDs of words that
  // don't encode to a single whole-word symbol. The cache should not be shared
  // between tokenizers that encode words differently. Not thread-safe
  class WordCache {
    friend BytePairEncoding;

   public:
    inline explicit WordCache(size_t capacity = 32768)
        : sets_(std::bit_ceil(std::max<size_t>(capacity / WAYS_PER_SET, 1))) {}

    inline void clear() { std::ranges::fill(sets_, Set{}); }

   private:
    // Cache is 2-way set-associative
    static constexpr size_t WAYS_PER_SET = 2;
    static constexpr size_t MAX_WORD_SIZE = 13;
    static constexpr size_t MAX_TOKEN_ID_COUNT = 4;

    struct Entry {
      bool isMostRecentlyUsed = false;

      uint8_t wordSize = 0;
      std::array<char, MAX_WORD_SIZE> word = {};

      uint8_t tokenIdCount = 0;
      std::array<uint32_t, MAX_TOKEN_ID_COUNT> tokenIds = {};
    };
    static_assert(sizeof(Entry) == 32);

    // 64-byte (or bigger) cache line would contain entire Set, storing
    // WAYS_PER_SET (2) entries since sizeof(Entry) == 32
    struct alignas(64) Set {
      std::array<Entry, WAYS_PER_SET> entries = {};
    };

    // Since the size of sets_ is a power of 2 (see the constructor), the
    // indexed position is entirely determined by the low bits of the given
    // hash
    inline Set& getSet(uint64_t hash) { return sets_[hash & (sets_.size() - 1)]; }

    std::span<const uint32_t> find(std::string_view word, uint64_t hash);
    void insert(std::string_view word, uint64_t hash, std::span<const uint32_t> tokenIds);

    std::vector<Set> sets_;
  };

  std::vector<std::byte> toBytes() const;
  static BytePairEncoding fromBytes(std::span<const std::byte> bytes);

  std::vector<uint32_t> encode(std::string_view text) const;
  void encode(
      std::string_view text,
      std::vector<uint32_t>& tokenIds,
      std::optional<WordCache&> cache = std::nullopt
  ) const;
  TokenIdBatch encodeBatch(std::span<const std::string_view> texts, size_t threadCount = 1) const;

  std::vector<uint32_t> encodeNormalized(std::string_view text) const;
  void encodeNormalized(
      std::string_view text,
      std::vector<uint32_t>& tokenIds,
      std::optional<WordCache&> cache = std::nullopt
  ) const;

  std::vector<uint32_t> encodeParts(std::span<const TextOrMarkupToken> parts) const;
  void encodeParts(
      std::span<const TextOrMarkupToken> parts,
      std::vector<uint32_t>& tokenIds,
      std::optional<WordCache&> cache = std::nullopt
  ) const;

  std::string decode(
      std::span<const uint32_t> tokenIds,
      const BytePairEncodingDecodeOptions& options = {}
  ) const;
  void decode(
      std::span<const uint32_t> tokenIds,
      std::string& text,
      const BytePairEncodingDecodeOptions& options = {}
  ) const;
  std::vector<TextOrMarkupToken> decodeParts(
      std::span<const uint32_t> tokenIds,
      const BytePairEncodingDecodeOptions& options = {}
  ) const;

  std::optional<uint32_t> tokenToId(std::string_view token) const;
  std::optional<uint32_t> markupTokenToId(std::string_view token) const;

  bool operator==(const BytePairEncoding& other) const {
    return settings_ == other.settings_ &&
           normalizer_ == other.normalizer_ &&
           symbols_.values() == other.symbols_.values() &&
           pairKeyToMergedSymbolId_.values() == other.pairKeyToMergedSymbolId_.values() &&
           markupTokenToTokenId_.values() == other.markupTokenToTokenId_.values() &&
           symbolIdToTokenId_ == other.symbolIdToTokenId_;
  }

  std::vector<std::pair<std::string, std::string>> merges() const;

 private:
  explicit BytePairEncoding(
      Normalizer normalizer,
      BytePairEncodingSettings settings,
      StringKeySet symbols,
      ankerl::unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId,
      StringKeyMap<uint32_t> markupTokenToTokenId,
      std::vector<uint32_t> symbolIdToTokenId
  );

  static BytePairEncoding trainFromTexts(
      TextStream texts,
      const TrainOptions& options,
      const Settings& settings,
      Normalizer normalizer
  );

  std::string decodeSymbolText(
      std::string_view symbolText,
      bool removeLeadingWordPrefix,
      bool removeTrailingWordSuffix,
      bool restoreWordDelimiters,
      std::string_view invalidUtf8Replacement
  ) const;

  std::optional<uint32_t> findFallbackTokenId() const;

  const BytePairEncodingSettings settings_;

  // Serves as a bidirectional map between symbols and IDs. unordered_dense
  // stores elements in insertion order, so a symbol's ID is its index in
  // values(). We can use that id (index) to look into symbolIdToTokenId_
  const StringKeySet symbols_;

  // As with symbols_, this map's elements are stored contiguously in insertion
  // order. The pair's index is supposed to denote its rank
  const ankerl::unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId_;

  const StringKeyMap<uint32_t> markupTokenToTokenId_;
  const std::vector<uint32_t> symbolIdToTokenId_;

  // For decoding
  const std::vector<TokenSource> tokenIdToTokenSource_;

  const StringKeyMap<uint32_t> wordToWholeWordSymbolId_;
};
}  // namespace wdym
