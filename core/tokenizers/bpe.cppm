module;

#include <ankerl/unordered_dense.h>

export module wdym:bpe;

import std.compat;
import :containers;
import :normalization;
import :tokenizer;

export namespace wdym {
struct ByteFallback {
  bool operator==(const ByteFallback&) const = default;
};

// TODO: make user-defined literal
struct MarkupToken {
  std::string text;

  bool operator==(const MarkupToken&) const = default;
};

struct BytePairEncodingSettings {
  std::vector<std::string> markup_tokens = {};

  NormalizationSettings normalization = {};

  std::string word_delimiter = " ";

  // Prepended or adjoined to the start of each word
  std::string word_prefix = "▁";
  // Appended or adjoined to the end of each word
  std::string word_suffix = "";
  // Whether to adjoin the prefix to the first symbol of each word. If false,
  // it is prepended as a separate symbol
  bool adjoin_prefix = false;
  // Whether to adjoin the suffix to the last symbol of each word. If false, it
  // is appended as a separate symbol
  bool adjoin_suffix = true;

  std::variant<std::monostate, ByteFallback, std::string, MarkupToken> unknown_character_fallback =
      {};

  bool operator==(const BytePairEncodingSettings&) const = default;
};

struct BytePairEncodingTrainOptions {
  int max_vocabulary_size = 8000;
  BytePairEncodingSettings settings = {};
};

class BytePairEncoding : public TokenizerBase<BytePairEncoding, BytePairEncodingTrainOptions> {
 public:
  // Fixed-size cache to speed up encoding by storing symbol IDs of words that
  // don't encode to a single whole-word symbol. The cache should not be shared
  // between tokenizers that encode words differently. Not thread-safe
  class WordCache {
   public:
    inline explicit WordCache(size_t capacity = 32768)
        : sets_(std::bit_ceil(std::max<size_t>(capacity / WAYS_PER_SET, 1))) {}

    inline void clear() { std::ranges::fill(sets_, Set{}); }

   private:
    friend BytePairEncoding;

    // Cache is 2-way set-associative
    static constexpr size_t WAYS_PER_SET = 2;
    static constexpr size_t MAX_WORD_SIZE = 13;
    static constexpr size_t MAX_SYMBOL_ID_COUNT = 4;

    struct Entry {
      bool isMostRecentlyUsed = false;

      uint8_t wordSize = 0;
      std::array<char, MAX_WORD_SIZE> word = {};

      uint8_t symbolIdCount = 0;
      std::array<uint32_t, MAX_SYMBOL_ID_COUNT> symbolIds = {};
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
    void insert(std::string_view word, uint64_t hash, std::span<const uint32_t> symbolIds);

    std::vector<Set> sets_;
  };

  void save(const std::filesystem::path& path) const;
  static BytePairEncoding load(const std::filesystem::path& path);

  std::vector<uint32_t> encode(std::string_view text) const;
  void encode(std::string_view text, std::vector<uint32_t>& ids) const;
  void encode(std::string_view text, std::vector<uint32_t>& ids, WordCache& cache) const;

  std::string normalize(std::string_view text) const;

  std::vector<uint32_t> encode_normalized(std::string_view text) const;
  void encode_normalized(std::string_view text, std::vector<uint32_t>& ids) const;
  void encode_normalized(std::string_view text, std::vector<uint32_t>& ids, WordCache& cache) const;

  void decode() const;

  std::optional<uint32_t> token_to_id(std::string_view token) const;
  std::optional<uint32_t> markup_token_to_id(std::string_view token) const;

  bool operator==(const BytePairEncoding& other) const {
    return settings_ == other.settings_ &&
           symbols_.values() == other.symbols_.values() &&
           pairKeyToMergedSymbolId_.values() == other.pairKeyToMergedSymbolId_.values();
  }

  std::vector<std::pair<std::string, std::string>> merges() const;

 private:
  friend TokenizerBase;

  // TODO: use camel case for private members
  static BytePairEncoding train_from_texts(TextStream texts, const TrainOptions& options);

  explicit BytePairEncoding(
      BytePairEncodingSettings settings,
      StringKeySet symbols,
      ankerl::unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId
  );

  void encodeWords(std::string_view text, std::vector<uint32_t>& ids, WordCache* cache) const;

  std::optional<uint32_t> findTokenFallbackId() const;

  // NOTE: I'm really not a fan of pimpl. My general philosophy is that code shouldn't be contorted
  // for tests or builds

  const BytePairEncodingSettings settings_;

  // Serves as a bidirectional map between symbols and IDs. unordered_dense
  // stores elements contiguously in insertion order, so a symbol's ID is its
  // index in values()
  const StringKeySet symbols_;

  // As with symbols_, this map's elements are stored contiguously in insertion
  // order. The pair's index is supposed to denote its rank
  const ankerl::unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId_;

  const Normalizer normalizer_;
  const StringKeyMap<uint32_t> wordToWholeWordSymbolId_;
};
}  // namespace wdym

namespace wdym {
template <>
constexpr std::string_view tokenizer_name<BytePairEncoding> = "BytePairEncoding";
}  // namespace wdym
