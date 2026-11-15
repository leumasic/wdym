# What Do You Mean (wdym)

wdym is a C++ library for extracting meaning from text corpora, with bindings for several languages.

> ⚠️ The library is in early stages of development and likely to have breaking API changes.

## Installation

## Usage

We expose bindings for certain languages as documented in the table below.

| Language | Package | Install |
|---|---|---|
| Python | [`wdym`](https://pypi.org/project/wdym/) on PyPI | `pip install wdym` |
| Node.js | not published yet | |

### Python

TBD

### Node.js

TBD

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
