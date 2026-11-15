export module wdym:markup_token;

import std;

export namespace wdym {
struct MarkupToken {
  std::string text;

  bool operator==(const MarkupToken&) const = default;
};

using TextOrMarkupToken = std::variant<std::string, MarkupToken>;

namespace literals {
inline MarkupToken operator""_markup(const char* text, std::size_t size) {
  return MarkupToken{std::string(text, size)};
}
}  // namespace literals
}  // namespace wdym
