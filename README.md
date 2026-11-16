# What Do You Mean (wdym)

wdym is a C++ tokenizer library with bindings for several languages.

> ⚠️ The library is in early stages of development and likely to have breaking API changes.

## Features

- ⚡ Fast training and encoding
- 🧩 Custom text normalization driven by replacement rules, along with built-in presets
- 🛡️ Markup tokens kept separate from text, so user input can't inject them
- 🔌 Bindings for Python and Node.js

## Getting started

- [Python](bindings/python/README.md)
- [Node.js](bindings/typescript/README.md)

### C++

> [!NOTE]
> **Requirement**: C++26 (GCC 16 or newer) and CMake 4.5 or newer.

Add wdym to your CMake project:

```cmake
include(FetchContent)
FetchContent_Declare(wdym
  GIT_REPOSITORY https://github.com/leumasic/wdym.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(wdym)

target_link_libraries(your_target PRIVATE wdym::wdym) # replace your_target
```

```cpp
import std;
import wdym;

using wdym::BytePairEncoding;

int main() {
  const std::vector<std::string> texts = {"hello world", "the quick brown fox"};
  const auto tokenizer = BytePairEncoding::train(texts, {.maxVocabularySize = 300});

  // Alternatively, train the tokenizer from a list of files
  const auto tokenizerTrainedFromFiles =
      BytePairEncoding::trainFromFiles({"first.txt", "second.txt"}, {.maxVocabularySize = 300});

  const std::vector<std::uint32_t> tokenIds = tokenizer.encode("hello world");
  std::println("{} {}", tokenIds, tokenizer.decode(tokenIds));

  tokenizer.save("tokenizer.wdym");
  const auto loaded = BytePairEncoding::load("tokenizer.wdym");
}
```

## Documentation

TBD

## Benchmarks

TBD

## Building from source

> [!NOTE]
> GCC >= 16 and CMake >= 4.5 and Ninja.

Configure, build and test the library with the `dev` preset:

```sh
cmake --workflow --preset dev
```

For a ready-made environment, the `.dev` folder has a container image with these tools installed, along with a compose file.

## Roadmap

The following is a non-comprehensive list of additions I'd like to work on.

### Tokenizers

- [ ] Adding more tokenizers (e.g. Unigram, WordPiece and byte-level BPE)
- [ ] Adding more segmentation methods (e.g. longest prefix, fewest tokens). See "Greed is All You Need" (Uzan et al.)
- [ ] Extending trained tokenizers by training on more data
- [ ] BPE-dropout

### Encoding and decoding

- [ ] Token offsets for spans of original text where each token came from
- [ ] Opt-in recognition of markup tokens written in plain text when encoding
- [ ] Character classes, such as digits or punctuation, that training never merges across
- [ ] Settings for repeated, leading and trailing word delimiters
- [ ] Built-in normalization rules, such as NFKC and case folding
- [ ] Parallel encoding of batches of texts
- [ ] Looking up a token's text from its ID

### Bindings

- [ ] Python: pickling, encoding and decoding of text and markup token parts, word cache, and NumPy arrays
- [ ] Bindings for JavaScript runtimes (e.g. Node.js, Bun, etc)
- [ ] C API

## License

wdym is released under the [MIT License](LICENSE).
