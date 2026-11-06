module;

#include <darts.h>

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

struct RuleMatch {
  size_t position;
  size_t inputSize;
  std::string_view output;
};

bool isAsciiByte(char byte) {
  // static_cast to prevent integral promotion and force zero extension instead
  return static_cast<unsigned char>(byte) <= 0x7F;
}

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
Utf8Sequence readUtf8Sequence(std::string_view text) {
  const auto leadingByte = static_cast<unsigned char>(text[0]);
  if (isAsciiByte(leadingByte))
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

  while (position < text.size() && isAsciiByte(text[position]))
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

inline bool isValidUtf8(std::string_view text) {
  return !findInvalidUtf8(text, 0).has_value();
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

std::string_view getRuleOutput(const wdym::CompiledNormalizationStep& step, size_t ruleIndex) {
  const uint32_t outputStart = step.outputOffsets[ruleIndex];
  return std::string_view(step.outputs)
      .substr(outputStart, step.outputOffsets[ruleIndex + 1] - outputStart);
}

std::optional<RuleMatch> findRuleMatch(
    std::string_view text,
    size_t position,
    const Darts::DoubleArray& trie,
    const wdym::CompiledNormalizationStep& step
) {
  for (; position < text.size(); position += readUtf8Sequence(text.substr(position)).length) {
    if (!step.hasAsciiOnlyInput) {
      const size_t nonAsciiPosition = skipAscii(text, position);
      if (nonAsciiPosition == text.size())
        return std::nullopt;

      if (nonAsciiPosition - position > step.maxLeadingAsciiSize)
        position = nonAsciiPosition - step.maxLeadingAsciiSize;
    }

    if (!step.isInputStartByte[static_cast<unsigned char>(text[position])])
      continue;

    const auto longestInput = trie.commonLongestPrefixSearch<Darts::DoubleArray::result_pair_type>(
        text.data() + position, text.size() - position
    );

    if (longestInput.value >= 0)
      return RuleMatch{
          .position = position,
          .inputSize = longestInput.length,
          .output = getRuleOutput(step, static_cast<size_t>(longestInput.value)),
      };
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

  Darts::DoubleArray trie;
  trie.set_array(step.trieUnits.data(), step.trieUnits.size());

  auto match = findRuleMatch(text, 0, trie, step);
  if (!match.has_value())
    return false;

  normalizedBuffer.clear();
  size_t copiedUntil = 0;

  while (match.has_value()) {
    normalizedBuffer.append(text.substr(copiedUntil, match->position - copiedUntil));
    normalizedBuffer.append(match->output);

    copiedUntil = match->position + match->inputSize;
    match = findRuleMatch(text, copiedUntil, trie, step);
  }

  normalizedBuffer.append(text.substr(copiedUntil));
  return true;
}

wdym::CompiledNormalizationStep compileNormalizationStep(const wdym::NormalizationStep& step) {
  wdym::CompiledNormalizationStep compiledStep;
  if (step.rules.empty())
    return compiledStep;

  std::vector<wdym::ReplacementRule> sortedRules = step.rules;
  std::ranges::sort(sortedRules, {}, &wdym::ReplacementRule::input);

  std::vector<const char*> inputs;
  std::vector<size_t> inputSizes;
  std::vector<int> ruleIndices;
  compiledStep.outputOffsets.push_back(0);

  for (size_t ruleIndex = 0; ruleIndex < sortedRules.size(); ++ruleIndex) {
    const wdym::ReplacementRule& rule = sortedRules[ruleIndex];
    inputs.push_back(rule.input.data());
    inputSizes.push_back(rule.input.size());
    ruleIndices.push_back(static_cast<int>(ruleIndex));

    compiledStep.isInputStartByte[static_cast<unsigned char>(rule.input.front())] = true;
    const auto firstNonAsciiByte = std::ranges::find_if_not(rule.input, isAsciiByte);
    if (firstNonAsciiByte == rule.input.end())
      compiledStep.hasAsciiOnlyInput = true;
    else
      compiledStep.maxLeadingAsciiSize = std::max(
          compiledStep.maxLeadingAsciiSize,
          static_cast<uint32_t>(firstNonAsciiByte - rule.input.begin())
      );

    compiledStep.outputs.append(rule.output);
    compiledStep.outputOffsets.push_back(static_cast<uint32_t>(compiledStep.outputs.size()));
  }

  Darts::DoubleArray trie;
  trie.build(inputs.size(), inputs.data(), inputSizes.data(), ruleIndices.data());

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
  std::string_view normalizedText = replaceInvalidUtf8(text, invalidUtf8Replacement_, buffer);
  std::string stepBuffer;

  for (const CompiledNormalizationStep& step : steps_) {
    if (!replaceRuleMatches(normalizedText, step, stepBuffer))
      continue;

    buffer.swap(stepBuffer);
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
