module;

// NOTE: for errno
#include <cerrno>

export module wdym:tokenizer;

import std;
import :normalization;

export namespace wdym {
using TextStream = std::generator<std::string_view>;

struct TokenIdBatch {
  std::vector<std::uint32_t> tokenIds;
  std::vector<std::size_t> tokenIdOffsets;
};

template <typename T>
concept Tokenizer = requires(
    const T& tokenizer,
    std::span<const std::byte> bytes,
    std::string_view text,
    std::span<const std::uint32_t> tokenIds,
    const typename T::DecodeOptions& decodeOptions
) {
  { tokenizer.toBytes() } -> std::same_as<std::vector<std::byte>>;
  { T::fromBytes(bytes) } -> std::same_as<T>;
  { tokenizer.encode(text) } -> std::same_as<std::vector<std::uint32_t>>;
  { tokenizer.decode(tokenIds, decodeOptions) } -> std::same_as<std::string>;
  { tokenizer.tokenToId(text) } -> std::same_as<std::optional<std::uint32_t>>;
  { tokenizer.markupTokenToId(text) } -> std::same_as<std::optional<std::uint32_t>>;
};

template <
    typename Derived,
    typename TrainOptionsType = std::monostate,
    typename SettingsType = std::monostate,
    typename EncodeOptionsType = std::monostate,
    typename DecodeOptionsType = std::monostate>
class TokenizerBase {
  // Prevents inheritance mistakes like
  // class SomeTokenizer : public TokenizerBase<OtherTokenizer, SomeOptions>
  friend Derived;

 public:
  using TrainOptions = TrainOptionsType;
  using Settings = SettingsType;
  using EncodeOptions = EncodeOptionsType;
  using DecodeOptions = DecodeOptionsType;

  static Derived train(
      TextStream texts,
      const TrainOptionsType& options = {},
      const SettingsType& settings = {},
      const NormalizationSettings& normalization = {}
  ) {
    return Derived::trainFromTexts(std::move(texts), options, settings, Normalizer(normalization));
  }

  template <std::ranges::input_range Texts>
    requires std::convertible_to<std::ranges::range_reference_t<Texts>, std::string_view>
  static Derived train(
      Texts&& texts,
      const TrainOptionsType& options = {},
      const SettingsType& settings = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train(streamTexts(texts), options, settings, normalization);
  }

  static Derived trainFromFiles(
      const std::filesystem::path& file,
      const TrainOptionsType& options = {},
      const SettingsType& settings = {},
      const NormalizationSettings& normalization = {}
  ) {
    return trainFromFiles(std::span(&file, 1), options, settings, normalization);
  }

  static Derived trainFromFiles(
      std::initializer_list<std::filesystem::path> files,
      const TrainOptionsType& options = {},
      const SettingsType& settings = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train(streamFileLines(files), options, settings, normalization);
  }

  template <std::ranges::input_range Files>
    requires std::convertible_to<std::ranges::range_reference_t<Files>, std::filesystem::path> &&
             (!std::same_as<std::remove_cvref_t<Files>, std::filesystem::path>)
  static Derived trainFromFiles(
      Files&& files,
      const TrainOptionsType& options = {},
      const SettingsType& settings = {},
      const NormalizationSettings& normalization = {}
  ) {
    return train(streamFileLines(files), options, settings, normalization);
  }

  void save(const std::filesystem::path& path) const {
    const std::vector<std::byte> bytes = static_cast<const Derived&>(*this).toBytes();

    std::ofstream file(path, std::ios::binary);
    file.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    file.flush();

    if (!file)
      throw std::filesystem::filesystem_error(
          "could not write tokenizer file", path, std::error_code(errno, std::generic_category())
      );
  }

  static Derived load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
      throw std::filesystem::filesystem_error(
          "could not open tokenizer file", path, std::error_code(errno, std::generic_category())
      );

    std::vector<std::byte> bytes(std::filesystem::file_size(path));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    if (!file)
      throw std::filesystem::filesystem_error(
          "could not read tokenizer file", path, std::error_code(errno, std::generic_category())
      );

    return Derived::fromBytes(bytes);
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
        requires(
            TextStream texts,
            const TrainOptionsType& options,
            const SettingsType& settings,
            Normalizer normalizer
        ) {
          {
            Derived::trainFromTexts(std::move(texts), options, settings, std::move(normalizer))
          } -> std::same_as<Derived>;
        },
        "tokenizers must implement trainFromTexts"
    );
  };
};

}  // namespace wdym
