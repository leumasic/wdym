module wdym:utf8;

import std;

namespace wdym {
struct Utf8SequenceValidity {
  std::size_t validLength;
  bool isValid;
};

struct InvalidUtf8SequenceLocation {
  std::size_t position;
  std::size_t validLength;
};

inline bool isAsciiByte(char byte) {
  // static_cast to prevent sign extension and use zero extension instead
  return static_cast<unsigned char>(byte) <= 0x7F;
}

inline Utf8SequenceValidity checkUtf8SequenceValidity(std::string_view text) {
  // Well-formed UTF-8 byte sequences (Unicode Standard, Table 3-7):
  // https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G27506
  //
  // Code points           Byte 1   Byte 2   Byte 3   Byte 4
  // U+0000..U+007F        00..7F
  // U+0080..U+07FF        C2..DF   80..BF
  // U+0800..U+0FFF        E0       A0..BF   80..BF
  // U+1000..U+CFFF        E1..EC   80..BF   80..BF
  // U+D000..U+D7FF        ED       80..9F   80..BF
  // U+E000..U+FFFF        EE..EF   80..BF   80..BF
  // U+10000..U+3FFFF      F0       90..BF   80..BF   80..BF
  // U+40000..U+FFFFF      F1..F3   80..BF   80..BF   80..BF
  // U+100000..U+10FFFF    F4       80..8F   80..BF   80..BF
  const auto leadingByte = static_cast<unsigned char>(text[0]);
  if (isAsciiByte(leadingByte))
    return {.validLength = 1, .isValid = true};

  std::size_t expectedLength = 0;
  unsigned char lowestContinuationByte = 0x80;
  unsigned char highestContinuationByte = 0xBF;

  if (leadingByte >= 0xC2 && leadingByte <= 0xDF) {
    expectedLength = 2;
  } else if (leadingByte >= 0xE0 && leadingByte <= 0xEF) {
    expectedLength = 3;
    if (leadingByte == 0xE0)
      lowestContinuationByte = 0xA0;
    else if (leadingByte == 0xED)
      highestContinuationByte = 0x9F;
  } else if (leadingByte >= 0xF0 && leadingByte <= 0xF4) {
    expectedLength = 4;
    if (leadingByte == 0xF0)
      lowestContinuationByte = 0x90;
    else if (leadingByte == 0xF4)
      highestContinuationByte = 0x8F;
  } else {
    return {.validLength = 0, .isValid = false};
  }

  for (std::size_t length = 1; length < expectedLength; ++length) {
    if (length == text.size())
      return {.validLength = length, .isValid = false};

    const auto byte = static_cast<unsigned char>(text[length]);
    if (byte < lowestContinuationByte || byte > highestContinuationByte)
      return {.validLength = length, .isValid = false};

    lowestContinuationByte = 0x80;
    highestContinuationByte = 0xBF;
  }

  return {.validLength = expectedLength, .isValid = true};
}

/**
 * Returns the position of the first non-ASCII byte at or after position,
 * or text.size() if there is none.
 */
inline std::size_t findNonAsciiPosition(std::string_view text, std::size_t position) {
  constexpr std::uint64_t NON_ASCII_BITS_MASK = 0x8080808080808080;

  while (position + sizeof(std::uint64_t) <= text.size()) {
    std::uint64_t chunk;
    std::memcpy(&chunk, text.data() + position, sizeof(chunk));

    // if any byte of chunk is has a top bit equal to 1, then it's not ASCII
    if (chunk & NON_ASCII_BITS_MASK)
      break;

    position += sizeof(chunk);
  }

  while (position < text.size() && isAsciiByte(text[position]))
    ++position;

  return position;
}

inline std::optional<InvalidUtf8SequenceLocation> findInvalidUtf8SequenceLocation(
    std::string_view text,
    std::size_t position
) {
  for (
      position = findNonAsciiPosition(text, position); position < text.size();
      position = findNonAsciiPosition(text, position)
  ) {
    const auto sequenceValidity = checkUtf8SequenceValidity(text.substr(position));
    if (!sequenceValidity.isValid)
      return InvalidUtf8SequenceLocation{
          .position = position, .validLength = sequenceValidity.validLength
      };

    position += sequenceValidity.validLength;
  }

  return std::nullopt;
}

inline bool isValidUtf8(std::string_view text) {
  return !findInvalidUtf8SequenceLocation(text, 0).has_value();
}

inline bool replaceInvalidUtf8(
    std::string_view text,
    std::string_view replacement,
    std::string& normalizedBuffer
) {
  normalizedBuffer.clear();
  std::size_t nextCopyPosition = 0;

  while (
      const auto invalidSequenceLocation = findInvalidUtf8SequenceLocation(text, nextCopyPosition)
  ) {
    normalizedBuffer.append(
        text.substr(nextCopyPosition, invalidSequenceLocation->position - nextCopyPosition)
    );
    normalizedBuffer.append(replacement);

    nextCopyPosition = invalidSequenceLocation->position +
                       std::max<std::size_t>(invalidSequenceLocation->validLength, 1);
  }

  if (nextCopyPosition == 0)
    return false;

  normalizedBuffer.append(text.substr(nextCopyPosition));
  return true;
}
}  // namespace wdym
