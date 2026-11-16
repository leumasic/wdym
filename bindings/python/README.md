# What Do You Mean (wdym)

wdym is a library of fast subword tokenizers for language models.

> ⚠️ The library is in early stages of development and likely to have breaking API changes.

## Installation

Install [`wdym`](https://pypi.org/project/wdym/) from PyPI:

```sh
pip install wdym
```

## Usage

```python
from wdym import BytePairEncoding

tokenizer = BytePairEncoding.train(
    sentence_iterator=["hello world", "the quick brown fox"],
    max_vocabulary_size=300,
)

# Alternatively, train the tokenizer from a list of files
tokenizer_trained_from_files = BytePairEncoding.train(
    input=["first.txt", "second.txt"],
    max_vocabulary_size=300,
)

token_ids = tokenizer.encode("hello world")
print(token_ids, tokenizer.decode(token_ids))

tokenizer.save("tokenizer.wdym")
loaded = BytePairEncoding.load("tokenizer.wdym")
```
