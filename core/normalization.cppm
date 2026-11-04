export module wdym:normalization;

import std;

export namespace wdym {
struct NormalizationSettings {
  std::string invalid_utf8_replacement = "�";

  bool operator==(const NormalizationSettings&) const = default;
};
}  // namespace wdym

namespace wdym {
void validateNormalizationSettings(const NormalizationSettings& settings);

std::string_view normalize(
    std::string_view text,
    const NormalizationSettings& settings,
    std::string& buffer
);
}  // namespace wdym
