module;

#include <cerrno>

export module wdym:tokenizer;

import std;

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

  static Derived train(TextStream texts, const Options& options = {}) {
    return Derived::train_from_texts(std::move(texts), options);
  }

  template <std::ranges::input_range Texts>
    requires std::convertible_to<std::ranges::range_reference_t<Texts>, std::string_view>
  static Derived train(Texts&& texts, const Options& options = {}) {
    return train(streamTexts(texts), options);
  }

  static Derived train_from_files(const std::filesystem::path& file, const Options& options = {}) {
    return train_from_files(std::span(&file, 1), options);
  }

  // Without this, train_from_files({"a.txt", "b.txt"}) would build a single path from the two
  // pointers as an iterator range
  static Derived train_from_files(
      std::initializer_list<std::filesystem::path> files,
      const Options& options = {}
  ) {
    return train(streamFileLines(files), options);
  }

  template <std::ranges::input_range Files>
    requires std::convertible_to<std::ranges::range_reference_t<Files>, std::filesystem::path>
             // a path is itself a range of path components
             && (!std::same_as<std::remove_cvref_t<Files>, std::filesystem::path>)
  static Derived train_from_files(Files&& files, const Options& options = {}) {
    return train(streamFileLines(files), options);
  }

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
        // add newline delimiter back for tokenizers who train with this added information
        if (!file.eof())
          line.push_back('\n');

        co_yield std::string_view(line);
      }
    }
  }

  TokenizerBase() {
    static_assert(Tokenizer<Derived>, "tokenizers must implement the Tokenizer concept");
    static_assert(
        requires(TextStream texts, const Options& options) {
          { Derived::train_from_texts(std::move(texts), options) } -> std::same_as<Derived>;
        }, "tokenizers must implement train_from_texts"
    );
  };
  // Prevents inheritance mistakes like
  // class SomeTokenizer : public TokenizerBase<OtherTokenizer, SomeOptions>
  friend Derived;
};

}  // namespace wdym
