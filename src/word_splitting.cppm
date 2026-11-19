module wdym:word_splitting;

import std;
import :bpe;

namespace wdym {
// Really wanted this function (and splitWordSymbols) to return a generator,
// which would be nicer to use at call sites. But generators can't be inlined
// (as opposed to this callback-form function) so there is a performance hit.
// To revisit...
template <std::invocable<std::string_view> OnWord>
void splitWords(
    std::string_view text,
    std::string_view delimiter,
    ExtraWordDelimiters extraDelimiters,
    OnWord&& onWord
) {
  if (text.empty())
    return;

  if (delimiter.empty()) {
    onWord(text);
    return;
  }

  auto words = std::views::split(text, delimiter) |
               std::views::filter([](const auto word) { return !word.empty(); });

  switch (extraDelimiters) {
    case ExtraWordDelimiters::Drop:
      for (const auto word : words)
        onWord(std::string_view(word));
      return;

    case ExtraWordDelimiters::PrefixNextWord: {
      size_t wordWithExtraDelimitersStart = 0;

      for (const auto word : words) {
        const auto wordEnd = static_cast<size_t>(word.end() - text.begin());
        onWord(text.substr(wordWithExtraDelimitersStart, wordEnd - wordWithExtraDelimitersStart));
        wordWithExtraDelimitersStart = wordEnd + delimiter.size();
      }

      if (wordWithExtraDelimitersStart <= text.size())
        onWord(text.substr(wordWithExtraDelimitersStart));

      return;
    }

    case ExtraWordDelimiters::SuffixPreviousWord: {
      std::optional<size_t> previousWordStart;

      for (const auto word : words) {
        const auto wordStart = static_cast<size_t>(word.begin() - text.begin());
        const size_t wordWithExtraDelimitersStart = previousWordStart.value_or(0);

        if (previousWordStart.has_value() || wordStart > 0)
          onWord(text.substr(
              wordWithExtraDelimitersStart,
              wordStart - delimiter.size() - wordWithExtraDelimitersStart
          ));

        previousWordStart = wordStart;
      }

      onWord(text.substr(previousWordStart.value_or(0)));
      return;
    }
  }
}
}  // namespace wdym
