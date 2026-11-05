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

struct NormalizationSettings {
  std::string invalid_utf8_replacement = "�";
  std::vector<NormalizationStep> steps = {};

  bool operator==(const NormalizationSettings&) const = default;
};
}  // namespace wdym

namespace wdym {
struct CompiledNormalizationStep {
  std::vector<std::uint32_t> trieUnits;
  std::string outputs;
  std::vector<std::uint32_t> outputOffsets;
  std::array<bool, 256> isInputStartByte = {};
  bool hasAsciiOnlyInput = false;
  std::uint32_t maxLeadingAsciiSize = 0;
};

class Normalizer {
 public:
  explicit Normalizer(const NormalizationSettings& settings);

  std::string_view normalize(std::string_view text, std::string& buffer) const;

 private:
  const std::string invalidUtf8Replacement_;
  const std::vector<CompiledNormalizationStep> steps_;
};

void validateNormalizationSettings(const NormalizationSettings& settings);
}  // namespace wdym
