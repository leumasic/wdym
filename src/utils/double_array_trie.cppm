module wdym:double_array_trie;

import std;

namespace wdym {
// Each unit stores a 32-bit layout:
//  - Bits [0, 7]:   label, the byte that leads into this unit
//                   (the equivalent of check array elements)
//
//  - Bit 8:         hasLeaf, set when a key ends at this node. That key's leaf unit is at
//                   node ^ offset
//
//  - Bit 9:         when set, offset is shifted left by 8 bits (multiplied by 256)
//                   for larger offsets
//
//  - Bits [10, 30]: offset, the child unit for byte c is at node ^ offset ^ c
//                   (the equivalent of base array elements)
//
//  - Bit 31:        set on leaf units, whose bits [0, 30] hold the keyIndex instead
//
// For more information, see the DoubleArrayUnit class in
// https://github.com/s-yata/darts-clone/blob/master/include/darts.h
class DoubleArrayTrieUnit {
 public:
  inline explicit DoubleArrayTrieUnit(std::uint32_t bits) : bits_(bits) {}

  inline std::uint32_t label() const { return bits_ & ((1U << 31) | 0xFF); }

  inline bool hasLeaf() const { return (bits_ >> 8) & 1; }

  inline std::uint32_t offset() const { return (bits_ >> 10) << ((bits_ & (1U << 9)) >> 6); }

  inline std::uint32_t keyIndex() const { return bits_ & ((1U << 31) - 1); }

  inline bool isLeaf() const { return bits_ >> 31; }

 private:
  std::uint32_t bits_;
};

struct LongestPrefixMatch {
  std::size_t prefixSize;
  std::uint32_t keyIndex;
};

inline std::optional<LongestPrefixMatch> findLongestPrefixMatch(
    // Trie units, as built by darts-clone which encodes base and check lists
    // expected of double-array tries
    std::span<const std::uint32_t> trieUnits,
    std::string_view text
) {
  std::optional<LongestPrefixMatch> longestPrefixMatch;
  std::uint32_t position = DoubleArrayTrieUnit(trieUnits[0]).offset();

  for (std::size_t byteIndex = 0; byteIndex < text.size(); ++byteIndex) {
    const auto byte = static_cast<unsigned char>(text[byteIndex]);
    position ^= byte;

    const DoubleArrayTrieUnit unit(trieUnits[position]);
    if (unit.label() != byte)
      break;

    position ^= unit.offset();
    if (unit.hasLeaf())
      longestPrefixMatch = LongestPrefixMatch{
          .prefixSize = byteIndex + 1,
          .keyIndex = DoubleArrayTrieUnit(trieUnits[position]).keyIndex(),
      };
  }

  return longestPrefixMatch;
}

inline bool isValidTrie(std::span<const std::uint32_t> trieUnits, std::size_t keyCount) {
  constexpr std::size_t BLOCK_SIZE = 256;
  if (trieUnits.empty() ||
      trieUnits.size() % BLOCK_SIZE != 0)
    return false;

  for (std::size_t position = 0; position < trieUnits.size(); ++position) {
    const DoubleArrayTrieUnit unit(trieUnits[position]);

    if (unit.isLeaf()) {
      if (position == 0)
        return false;

      continue;
    }

    const std::size_t childPosition = position ^ unit.offset();
    if (childPosition >= trieUnits.size())
      return false;

    if (unit.hasLeaf()) {
      const DoubleArrayTrieUnit leaf(trieUnits[childPosition]);
      if (!leaf.isLeaf() || leaf.keyIndex() >= keyCount)
        return false;
    }
  }

  return true;
}
}  // namespace wdym
