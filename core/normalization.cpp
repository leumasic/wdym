module;

#include <darts.h>

module wdym;

import std.compat;
import :double_array_trie;
import :normalization;

namespace {
struct Utf8SequenceValidity {
  size_t validLength;
  bool isValid;
};

struct InvalidUtf8SequenceLocation {
  size_t position;
  size_t validLength;
};

struct ReplacementRuleMatch {
  size_t position;
  size_t inputSize;
  std::string_view output;
};

bool isAsciiByte(char byte) {
  // static_cast to prevent sign extension and use zero extension instead
  return static_cast<unsigned char>(byte) <= 0x7F;
}

Utf8SequenceValidity checkUtf8SequenceValidity(std::string_view text) {
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
    return {.validLength = 0, .isValid = false};
  }

  for (size_t length = 1; length < expectedLength; ++length) {
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
size_t findNonAsciiPosition(std::string_view text, size_t position) {
  constexpr uint64_t NON_ASCII_BITS_MASK = 0x8080808080808080;

  while (position + sizeof(uint64_t) <= text.size()) {
    uint64_t chunk;
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

std::optional<InvalidUtf8SequenceLocation> findInvalidUtf8SequenceLocation(
    std::string_view text,
    size_t position
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

bool replaceInvalidUtf8(
    std::string_view text,
    std::string_view replacement,
    std::string& normalizedBuffer
) {
  normalizedBuffer.clear();
  size_t nextCopyPosition = 0;

  while (
      const auto invalidSequenceLocation = findInvalidUtf8SequenceLocation(text, nextCopyPosition)
  ) {
    normalizedBuffer.append(
        text.substr(nextCopyPosition, invalidSequenceLocation->position - nextCopyPosition)
    );
    normalizedBuffer.append(replacement);

    nextCopyPosition = invalidSequenceLocation->position +
                       std::max<size_t>(invalidSequenceLocation->validLength, 1);
  }

  if (nextCopyPosition == 0)
    return false;

  normalizedBuffer.append(text.substr(nextCopyPosition));
  return true;
}

std::string_view getRuleOutput(const wdym::CompiledNormalizationStep& step, size_t ruleIndex) {
  const uint32_t outputStart = step.outputOffsets[ruleIndex];
  return std::string_view(step.outputs)
      .substr(outputStart, step.outputOffsets[ruleIndex + 1] - outputStart);
}

std::optional<ReplacementRuleMatch> findReplacementRuleMatch(
    std::string_view text,
    size_t position,
    const wdym::CompiledNormalizationStep& step
) {
  while (position < text.size()) {
    const auto longestInputMatch =
        wdym::findLongestPrefixMatch(step.trieUnits, text.substr(position));

    if (longestInputMatch.has_value())
      return ReplacementRuleMatch{
          .position = position,
          .inputSize = longestInputMatch->prefixSize,
          .output = getRuleOutput(step, longestInputMatch->keyIndex),
      };

    // We assume that invalid utf8 characters have been removed from text so
    // that the lead byte's purported unicode sequence length is accurate
    position += std::max(1, std::countl_one(static_cast<unsigned char>(text[position])));
  }

  return std::nullopt;
}

bool replaceRuleMatches(
    std::string_view text,
    const wdym::CompiledNormalizationStep& step,
    std::string& normalizedBuffer
) {
  if (step.trieUnits.empty())
    return false;

  normalizedBuffer.clear();
  size_t nextCopyPosition = 0;

  while (const auto replacementRuleMatch = findReplacementRuleMatch(text, nextCopyPosition, step)) {
    normalizedBuffer.append(
        text.substr(nextCopyPosition, replacementRuleMatch->position - nextCopyPosition)
    );
    normalizedBuffer.append(replacementRuleMatch->output);

    nextCopyPosition = replacementRuleMatch->position + replacementRuleMatch->inputSize;
  }

  if (nextCopyPosition == 0)
    return false;

  normalizedBuffer.append(text.substr(nextCopyPosition));
  return true;
}

wdym::CompiledNormalizationStep compileNormalizationStep(const wdym::NormalizationStep& step) {
  wdym::CompiledNormalizationStep compiledStep;
  if (step.rules.empty())
    return compiledStep;

  // Darts requires its keys sorted in byte order to build the trie
  // See https://github.com/s-yata/darts-clone/blob/master/doc/en/Interface.md#build-dictionary
  auto sortedRules = step.rules;
  std::ranges::sort(sortedRules, {}, &wdym::ReplacementRule::input);

  std::vector<const char*> inputs;
  std::vector<size_t> inputSizes;
  compiledStep.outputOffsets.push_back(0);

  for (const wdym::ReplacementRule& rule : sortedRules) {
    inputs.push_back(rule.input.data());
    inputSizes.push_back(rule.input.size());

    compiledStep.outputs.append(rule.output);
    compiledStep.outputOffsets.push_back(static_cast<uint32_t>(compiledStep.outputs.size()));
  }

  Darts::DoubleArray trie;
  trie.build(inputs.size(), inputs.data(), inputSizes.data());

  // The size of each darts unit is 4 bytes
  // See https://github.com/s-yata/darts-clone/blob/master/doc/en/Interface.md#get-size
  const auto* trieUnits = static_cast<const uint32_t*>(trie.array());
  compiledStep.trieUnits.assign(trieUnits, trieUnits + trie.size());

  return compiledStep;
}

std::vector<wdym::CompiledNormalizationStep> compileNormalizationSteps(
    const std::vector<wdym::NormalizationStep>& steps
) {
  std::vector<wdym::CompiledNormalizationStep> compiledSteps;
  compiledSteps.reserve(steps.size());

  for (const wdym::NormalizationStep& step : steps)
    compiledSteps.push_back(compileNormalizationStep(step));

  return compiledSteps;
}
}  // namespace

wdym::Normalizer::Normalizer(const NormalizationSettings& settings)
    : invalidUtf8Replacement_(settings.invalid_utf8_replacement),
      steps_(compileNormalizationSteps(settings.steps)) {}

std::string_view wdym::Normalizer::normalize(std::string_view text, std::string& buffer) const {
  std::string_view normalizedText =
      replaceInvalidUtf8(text, invalidUtf8Replacement_, buffer) ? std::string_view(buffer) : text;
  std::string stepOutputBuffer;

  for (const CompiledNormalizationStep& step : steps_) {
    if (!replaceRuleMatches(normalizedText, step, stepOutputBuffer))
      continue;

    buffer.swap(stepOutputBuffer);
    normalizedText = buffer;
  }

  return normalizedText;
}

void wdym::validateNormalizationSettings(const NormalizationSettings& settings) {
  if (!isValidUtf8(settings.invalid_utf8_replacement))
    throw std::invalid_argument(
        std::format(
            "invalid_utf8_replacement {:?} is not valid UTF-8", settings.invalid_utf8_replacement
        )
    );

  for (size_t stepIndex = 0; stepIndex < settings.steps.size(); ++stepIndex) {
    const NormalizationStep& step = settings.steps[stepIndex];

    if (!isValidUtf8(step.name))
      throw std::invalid_argument(
          std::format(
              "normalization step {} has a name {:?} that is not valid UTF-8", stepIndex, step.name
          )
      );

    const std::string stepDescription =
        step.name.empty() ? std::format("normalization step {}", stepIndex)
                          : std::format("normalization step {} ({:?})", stepIndex, step.name);
    std::unordered_set<std::string_view> inputs;

    for (const ReplacementRule& rule : step.rules) {
      if (rule.input.empty())
        throw std::invalid_argument(
            std::format("{} has a rule with an empty input", stepDescription)
        );

      if (rule.input.contains('\0'))
        throw std::invalid_argument(
            std::format(
                "{} has a rule input {:?} that contains disallowed U+0000", stepDescription,
                rule.input
            )
        );

      if (!isValidUtf8(rule.input))
        throw std::invalid_argument(
            std::format(
                "{} has a rule input {:?} that is not valid UTF-8", stepDescription, rule.input
            )
        );

      if (!isValidUtf8(rule.output))
        throw std::invalid_argument(
            std::format(
                "{} has a rule output {:?} that is not valid UTF-8", stepDescription, rule.output
            )
        );

      if (rule.input == rule.output)
        throw std::invalid_argument(
            std::format("{} has a rule that maps {:?} to itself", stepDescription, rule.input)
        );

      if (!inputs.insert(rule.input).second)
        throw std::invalid_argument(
            std::format("{} has more than one rule with input {:?}", stepDescription, rule.input)
        );
    }
  }
}
