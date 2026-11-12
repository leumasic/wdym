module;

#include <darts.h>

module wdym;

import std.compat;
import :double_array_trie;
import :normalization;
import :utf8;

namespace {
struct ReplacementRuleMatch {
  size_t position;
  size_t inputSize;
  std::string_view output;
};

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
