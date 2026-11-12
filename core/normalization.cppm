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

struct NormalizationSettings {
  std::variant<std::string, KeepInvalidUtf8> invalid_utf8_fallback = "�";
  std::vector<NormalizationStep> steps = {};

  bool operator==(const NormalizationSettings&) const = default;
};
}  // namespace wdym

namespace wdym {
struct CompiledNormalizationStep {
  // Holds representation of a double-array trie built by darts-clone library
  // for looking up replacement rule inputs
  std::vector<std::uint32_t> trieUnits;

  // The concatenated outputs of all replacement rules in a NormalizationStep
  std::string outputs;
  // Offsets into the outputs string (the struct member) of each replacement rule's output.
  // Each index in the list corresponds to the rule index returned by a trie lookup, and the
  // value is where that rule's output starts in outputs
  std::vector<std::uint32_t> outputOffsets;
};

class Normalizer {
 public:
  explicit Normalizer(const NormalizationSettings& settings);

  std::string_view normalize(std::string_view text, std::string& buffer) const;

 private:
  const std::variant<std::string, KeepInvalidUtf8> invalidUtf8Fallback_;
  const std::vector<CompiledNormalizationStep> steps_;
};

void validateNormalizationSettings(const NormalizationSettings& settings);
}  // namespace wdym
