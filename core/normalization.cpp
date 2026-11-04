module wdym;

import std.compat;
import :normalization;

namespace {
constexpr uint64_t ASCII_CHUNK_HIGH_BITS = 0x8080808080808080;

struct Utf8Sequence {
  size_t length;
  bool isValid;
};

struct InvalidUtf8Sequence {
  size_t position;
  size_t length;
};

Utf8Sequence readUtf8Sequence(std::string_view text) {
  const auto leadingByte = static_cast<unsigned char>(text[0]);
  if (leadingByte < 0x80)
    return {.length = 1, .isValid = true};

  size_t expectedLength = 0;
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
    return {.length = 1, .isValid = false};
  }

  for (size_t length = 1; length < expectedLength; ++length) {
    if (length == text.size())
      return {.length = length, .isValid = false};

    const auto byte = static_cast<unsigned char>(text[length]);
    if (byte < lowestContinuationByte || byte > highestContinuationByte)
      return {.length = length, .isValid = false};

    lowestContinuationByte = 0x80;
    highestContinuationByte = 0xBF;
  }

  return {.length = expectedLength, .isValid = true};
}

size_t skipAscii(std::string_view text, size_t position) {
  while (position + sizeof(uint64_t) <= text.size()) {
    uint64_t chunk;
    std::memcpy(&chunk, text.data() + position, sizeof(chunk));
    if (chunk & ASCII_CHUNK_HIGH_BITS)
      break;

    position += sizeof(chunk);
  }

  while (position < text.size() && static_cast<unsigned char>(text[position]) < 0x80)
    ++position;

  return position;
}

std::optional<InvalidUtf8Sequence> findInvalidUtf8(std::string_view text, size_t position) {
  for (position = skipAscii(text, position); position < text.size();
       position = skipAscii(text, position)) {
    const auto sequence = readUtf8Sequence(text.substr(position));
    if (!sequence.isValid)
      return InvalidUtf8Sequence{.position = position, .length = sequence.length};

    position += sequence.length;
  }

  return std::nullopt;
}

std::string_view replaceInvalidUtf8(
    std::string_view text,
    std::string_view replacement,
    std::string& normalizedBuffer
) {
  auto invalidSequence = findInvalidUtf8(text, 0);
  if (!invalidSequence.has_value())
    return text;

  normalizedBuffer.clear();
  size_t copiedUntil = 0;

  while (invalidSequence.has_value()) {
    normalizedBuffer.append(text.substr(copiedUntil, invalidSequence->position - copiedUntil));
    normalizedBuffer.append(replacement);

    copiedUntil = invalidSequence->position + invalidSequence->length;
    invalidSequence = findInvalidUtf8(text, copiedUntil);
  }

  normalizedBuffer.append(text.substr(copiedUntil));
  return normalizedBuffer;
}
}  // namespace

void wdym::validateNormalizationSettings(const NormalizationSettings& settings) {
  if (findInvalidUtf8(settings.invalid_utf8_replacement, 0).has_value())
    throw std::invalid_argument(
        std::format(
            "invalid_utf8_replacement {:?} is not valid UTF-8", settings.invalid_utf8_replacement
        )
    );
}

std::string_view wdym::normalize(
    std::string_view text,
    const NormalizationSettings& settings,
    std::string& buffer
) {
  // TODO: add other normalization steps here
  return replaceInvalidUtf8(text, settings.invalid_utf8_replacement, buffer);
}
