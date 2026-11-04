module;

#include <ankerl/unordered_dense.h>

export module wdym:bpe;

import std.compat;
import :containers;
import :normalization;
import :tokenizer;

namespace wdym {
struct PairMerge {
  uint32_t rank;
  uint32_t mergedSymbolId;
};

struct FrequentPairMerges {
  static constexpr size_t SLOT_COUNT = 256;

  std::vector<uint16_t> symbolIdToSlot;
  std::vector<PairMerge> slotPairToMerge;
};
}  // namespace wdym

export namespace wdym {
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

  // When encoding, whether to turn characters unseen during training into
  // individual UTF-8 bytes, each byte having its own reserved symbol.
  // Must be false if token_fallback is non-empty
  bool use_byte_fallback = false;

  // When encoding, the token to turn characters unseen during training into,
  // commonly called the "unknown token". If empty, these characters are dropped.
  // Must be empty if use_byte_fallback is true
  std::string token_fallback = "";

  bool operator==(const BytePairEncodingSettings&) const = default;
};

struct BytePairEncodingTrainOptions {
  int max_vocabulary_size = 8000;
  BytePairEncodingSettings settings = {};
};

class BytePairEncoding : public TokenizerBase<BytePairEncoding, BytePairEncodingTrainOptions> {
 public:
  class Cache {
   public:
    explicit Cache(size_t capacity = 32768);

    void clear();

   private:
    friend BytePairEncoding;

    static constexpr size_t MAX_WORD_SIZE = 13;
    static constexpr size_t MAX_ID_COUNT = 4;

    struct alignas(32) Entry {
      uint8_t wordSize = 0;
      uint8_t idCount = 0;
      bool isRecent = false;
      std::array<char, MAX_WORD_SIZE> word = {};
      std::array<uint32_t, MAX_ID_COUNT> ids = {};
    };

    std::span<const uint32_t> find(std::string_view word, uint64_t hash);
    void insert(std::string_view word, uint64_t hash, std::span<const uint32_t> ids);

    std::vector<Entry> entries_;
    uint64_t tokenizerId_ = 0;
  };

  void save(const std::filesystem::path& path) const;
  static BytePairEncoding load(const std::filesystem::path& path);

  std::vector<uint32_t> encode(std::string_view text) const;
  void encode(std::string_view text, std::vector<uint32_t>& ids) const;
  void encode(std::string_view text, std::vector<uint32_t>& ids, Cache& cache) const;

  std::string normalize(std::string_view text) const;

  std::vector<uint32_t> encode_normalized(std::string_view text) const;
  void encode_normalized(std::string_view text, std::vector<uint32_t>& ids) const;
  void encode_normalized(std::string_view text, std::vector<uint32_t>& ids, Cache& cache) const;

  void decode() const;

  std::optional<uint32_t> token_to_id(std::string_view token) const;

  bool operator==(const BytePairEncoding& other) const {
    return settings_ == other.settings_ && symbols_.values() == other.symbols_.values() &&
           pairKeyToMergedSymbolId_.values() == other.pairKeyToMergedSymbolId_.values();
  }

  std::vector<std::pair<std::string, std::string>> merges() const;

 private:
  friend TokenizerBase;

  static BytePairEncoding train_from_texts(TextStream texts, const TrainOptions& options);

  explicit BytePairEncoding(
      BytePairEncodingSettings settings,
      StringKeySet symbols,
      ankerl::unordered_dense::map<uint64_t, uint32_t> pairKeyToMergedSymbolId
  );

  void encodeWords(std::string_view text, std::vector<uint32_t>& ids, Cache* cache) const;

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

  const std::array<uint32_t, 128> asciiCharacterToSymbolId_;
  const FrequentPairMerges frequentPairMerges_;
  const StringKeyMap<uint32_t> wholeWordToSymbolId_;

  const uint64_t instanceId_;
};
}  // namespace wdym

namespace wdym {
template <>
constexpr std::string_view tokenizer_name<BytePairEncoding> = "BytePairEncoding";
}  // namespace wdym
