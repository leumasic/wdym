module;

// NOTE: for errno
#include <cerrno>

export module wdym:tokenizer;

import std;
import :normalization;

export namespace wdym {
using TextStream = std::generator<std::string_view>;

template <typename T>
concept Tokenizer = requires(const T& tokenizer, const std::filesystem::path& path) {
  { tokenizer.save(path) } -> std::same_as<void>;
};

template <Tokenizer T>
inline constexpr std::string_view tokenizer_name = {};

template <typename Derived, typename Options>
class TokenizerBase {
 public:
  using TrainOptions = Options;

  static Derived train(
      TextStream texts,
      const Options& options = {},
      const NormalizationSettings& normalization = {}
  ) {
    return Derived::train_from_texts(std::move(texts), options, Normalizer(normalization));
  }

  template <std::ranges::input_range Texts>
    requires std::convertible_to<std::ranges::range_reference_t<Texts>, std::string_view>
  static Derived train(
      Texts&& texts,
      const Options& options = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train(streamTexts(texts), options, normalization);
  }

  static Derived train_from_files(
      const std::filesystem::path& file,
      const Options& options = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train_from_files(std::span(&file, 1), options, normalization);
  }

  static Derived train_from_files(
      std::initializer_list<std::filesystem::path> files,
      const Options& options = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train(streamFileLines(files), options, normalization);
  }

  template <std::ranges::input_range Files>
    requires std::convertible_to<std::ranges::range_reference_t<Files>, std::filesystem::path> &&
             (!std::same_as<std::remove_cvref_t<Files>, std::filesystem::path>)
  static Derived train_from_files(
      Files&& files,
      const Options& options = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train(streamFileLines(files), options, normalization);
  }

  std::string normalize(std::string_view text) const {
    std::string normalizationBuffer;
    return std::string(normalizer_.normalize(text, normalizationBuffer));
  }

 protected:
  const Normalizer normalizer_;

 private:
  template <typename Texts>
  static TextStream streamTexts(Texts& texts) {
    for (auto&& text : texts)
      co_yield std::string_view(text);
  }

  template <typename Files>
  static TextStream streamFileLines(Files& files) {
    std::string line;

    for (const std::filesystem::path& path : files) {
      std::ifstream file(path, std::ios::binary);
      if (!file.is_open())
        throw std::filesystem::filesystem_error(
            "could not open training file", path, std::error_code(errno, std::generic_category())
        );

      while (std::getline(file, line)) {
        // add newline delimiter back for tokenizers that train with this added information
        if (!file.eof())
          line.push_back('\n');

        co_yield std::string_view(line);
      }
    }
  }

  explicit TokenizerBase(Normalizer normalizer) : normalizer_(std::move(normalizer)) {
    static_assert(Tokenizer<Derived>, "tokenizers must implement the Tokenizer concept");
    static_assert(
        requires(TextStream texts, const Options& options, Normalizer normalizer) {
          {
            Derived::train_from_texts(std::move(texts), options, std::move(normalizer))
          } -> std::same_as<Derived>;
        }, "tokenizers must implement train_from_texts"
    );
  };
  // Prevents inheritance mistakes like
  // class SomeTokenizer : public TokenizerBase<OtherTokenizer, SomeOptions>
  friend Derived;
};

}  // namespace wdym
