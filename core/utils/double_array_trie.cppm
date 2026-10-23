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
}  // namespace wdym
