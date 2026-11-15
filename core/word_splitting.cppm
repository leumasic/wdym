module wdym:word_splitting;

import std;

namespace wdym {
// Really wanted this function (and splitWordSymbols) to return a generator,
// which would be nicer to use at call sites. But generators can't be inlined
// (as opposed to this callback-form function) so there is a performance hit.
// To revisit...
template <std::invocable<std::string_view> OnWord>
void splitWords(std::string_view text, std::string_view delimiter, OnWord&& onWord) {
  if (delimiter.empty()) {
    if (!text.empty())
      onWord(text);

    return;
  }

  for (const auto word : std::views::split(text, delimiter))
    if (!word.empty())
      onWord(std::string_view(word));
}
}  // namespace wdym
