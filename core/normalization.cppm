export module wdym:normalization;

import std;

export namespace wdym {
struct ReplacementRule {
  std::string input = "";
  std::string output = "";

  bool operator==(const ReplacementRule&) const = default;
};

struct NormalizationStep {
  std::string name = "";
  std::vector<ReplacementRule> rules = {};

  bool operator==(const NormalizationStep&) const = default;
};

struct KeepInvalidUtf8 {
  bool operator==(const KeepInvalidUtf8&) const = default;
};

using InvalidUtf8Fallback = std::variant<std::string, KeepInvalidUtf8>;

struct NormalizationSettings {
  InvalidUtf8Fallback invalid_utf8_fallback = "�";
  std::vector<NormalizationStep> steps = {};

  bool operator==(const NormalizationSettings&) const = default;
};
}  // namespace wdym

namespace wdym {
struct CompiledNormalizationStep {
  std::string name;

  // Holds representation of a double-array trie built by darts-clone library
  // for looking up replacement rule inputs
  std::vector<std::uint32_t> trieUnits;

  // The concatenated outputs of all replacement rules in a NormalizationStep
  std::string outputs;
  // Offsets into the outputs string (the struct member) of each replacement rule's output.
  // Each index in the list corresponds to the rule index returned by a trie lookup, and the
  // value is where that rule's output starts in outputs
  std::vector<std::uint32_t> outputOffsets;

  bool operator==(const CompiledNormalizationStep&) const = default;
};

class Normalizer {
 public:
  explicit Normalizer(const NormalizationSettings& settings);
  explicit Normalizer(
      std::vector<CompiledNormalizationStep> compiledSteps,
      InvalidUtf8Fallback fallback
  );

  std::string_view normalize(std::string_view text, std::string& buffer) const;

  bool operator==(const Normalizer&) const = default;

  const InvalidUtf8Fallback invalidUtf8Fallback;
  const std::vector<CompiledNormalizationStep> steps;
};
}  // namespace wdym
