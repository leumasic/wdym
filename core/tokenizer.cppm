module;

#include <cerrno>

export module wdym:tokenizer;

import std;

export namespace wdym {
using TextStream = std::generator<std::string_view>;

template <typename T>
concept Tokenizer = requires(
    const T& tokenizer,
    TextStream texts,
    const T::TrainOptions& options,
    const std::filesystem::path& path
) {
  { T::train_from_texts(std::move(texts), options) } -> std::same_as<T>;
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

  static Derived train(std::span<const std::filesystem::path> files, const Options& options = {}) {
    // TODO: perhaps use multithreading to read files on multiple threads
    return train(read_lines(files), options);
  }

 private:
  static TextStream read_lines(std::span<const std::filesystem::path> files) {
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
    static_assert(Tokenizer<Derived>, "tokenizers must implement the wdym::Tokenizer concept");
  };
  // Prevents inheritance mistakes like
  // class SomeTokenizer : public TokenizerBase<OtherTokenizer, SomeOptions>
  friend Derived;
};

}  // namespace wdym
