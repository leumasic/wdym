# What Do You Mean (wdym)

wdym is a C++ tokenizer library with bindings for several languages.

> ⚠️ The library is in early stages of development and likely to have breaking API changes.

## Features

- ⚡ Fast training and encoding
- 🧩 Custom text normalization driven by replacement rules
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

With every library configured to train equivalent BPE tokenizers of 32,000 tokens, on 50 MB of English web text from FineWeb for training and 5 MB for encoding:

| | wdym | SentencePiece | Hugging Face tokenizers |
|---|---|---|---|
| Encoding, 1 thread (MB/s) | 75.7 | 48.7 | 8.7 |
| Encoding, 32 threads (MB/s) | 961.7 | 629.7 | 64.4 |
| Training, 1 thread (seconds) | 1.0 | 3.1 | 5.9 |
| Training, 32 threads (seconds) | n/a | 1.4 | 4.4 |

Encoding is measured from C++ for wdym and for SentencePiece, through its new runtime SentencePiece Lite, and from Rust for Hugging Face tokenizers. wdym trains on one thread only.

See [benchmarks](benchmarks/README.md) for 128,000-token vocabularies, memory use, Python, the method, the hardware and how to run them yourself.

## Building from source

> [!NOTE]
> GCC >= 16 and CMake >= 4.5 and Ninja.

Configure, build and test the library with the `dev` preset:

```sh
cmake --workflow --preset dev
```

For a ready-made environment, the `.dev` folder has a container image with these tools installed, along with a compose file.

## Roadmap

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
- [ ] Bindings for other JavaScript runtimes (e.g. Bun, Deno)
- [ ] C API

## License

wdym is released under the [MIT License](LICENSE).
