# What Do You Mean (wdym)

wdym is a library of fast subword tokenizers for language models.

> ⚠️ The library is in early stages of development and likely to have breaking API changes.

## Installation

Install [`@leumasic/wdym`](https://www.npmjs.com/package/@leumasic/wdym) from npm:

```sh
npm install @leumasic/wdym
```

> [!NOTE]
> **Requirement**: Node.js 20 or newer on Linux with glibc, on x64 or arm64.

## Usage

The package includes TypeScript declarations.

```js
import { BytePairEncoding } from "@leumasic/wdym";

const tokenizer = await BytePairEncoding.train({
  sentenceIterator: ["hello world", "the quick brown fox"],
  maxVocabularySize: 300,
});

// Alternatively, train the tokenizer from a list of files
const tokenizerTrainedFromFiles = await BytePairEncoding.train({
  input: ["first.txt", "second.txt"],
  maxVocabularySize: 300,
});

const tokenIds = tokenizer.encode("hello world");
console.log(tokenIds, tokenizer.decode(tokenIds));

tokenizer.save("tokenizer.wdym");
const loaded = BytePairEncoding.load("tokenizer.wdym");
```
