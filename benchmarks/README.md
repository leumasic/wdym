# Benchmarks

These benchmarks compare wdym with [SentencePiece](https://github.com/google/sentencepiece), [SentencePiece Lite](https://github.com/google/sentencepiece/tree/master/lite) and [Hugging Face tokenizers](https://github.com/huggingface/tokenizers) on training and encoding speed for BPE tokenizers. Every library is called from its own language: C++ for wdym, SentencePiece and SentencePiece Lite, and Rust for Hugging Face tokenizers.

## What is measured

### Data

English web text from [FineWeb](https://huggingface.co/datasets/HuggingFaceFW/fineweb) (`sample-10BT`), downloaded by `fetch_data.py` through the Hugging Face datasets server:

| File | Documents | Size | SHA-256 |
|---|---|---|---|
| `train.txt` | 16,255 documents starting at row 1,000,000 | 50.0 MB | `3c619472cb621bd3e0d5057b72f6ae5a32e71bd2741beff2fa5775af77c95cac` |
| `test.txt` | 1,599 documents starting at row 0 | 5.0 MB | `35c9a05f29eec4c68eb337882cea9c8faefcea568d86fed4f60c7033bbc3de7f` |

Every library receives the same lines: each file is split on `\n`, and each line is one input text.

### Tokenizer settings

All three libraries are configured to train the same kind of tokenizer, so their speed can be compared:

- BPE with a vocabulary of 32,000 or 128,000 tokens
- no normalization
- words split on spaces, with `▁` prepended to every word
- no byte fallback, and no splitting by script or digits
- one thread, and again with all 32 hardware threads

| Library | Settings |
|---|---|
| wdym | defaults (`wordDelimiter = " "`, `wordPrefix = "▁"`) |
| SentencePiece | `spm_train` with `--model_type=bpe`, `--normalization_rule_name=identity`, `--character_coverage=1.0`, `--byte_fallback=false`, `--split_by_unicode_script=false`, `--split_by_number=false`, `--max_sentencepiece_length=512`, `--max_sentence_length=1048576`, `--input_sentence_size=0`, `--bos_id=-1`, `--eos_id=-1`, `--num_threads` set to the thread count |
| Hugging Face tokenizers | `BPE` model, `Metaspace::new('▁', PrependScheme::Always, true)` pre-tokenizer, thread pool sized to the thread count, parallelism switched off with one thread |

The token counts in the results show the tokenizers are equivalent: at each vocabulary size, the three libraries produce token counts within 0.03% of each other on the test text.

### Training

Each library trains once on `train.txt` in its own process: a C++ program for wdym, SentencePiece's `spm_train` tool, and a Rust program for Hugging Face tokenizers. The time and the peak resident memory are those of the whole process, which reads the file, trains and saves the tokenizer file. wdym trains on one thread only, so it has no rows with 32 threads.

### Encoding

Each library encodes every line of `test.txt` separately. Throughput is the UTF-8 size of the test text divided by the fastest of 5 passes.

wdym and SentencePiece Lite are called from C++, and Hugging Face tokenizers from Rust through `encode_fast` and `encode_batch_fast`, which skip character offsets. SentencePiece Lite encodes with the model trained by SentencePiece after converting it with Lite's `spm_to_fb` tool.

- With one thread, each line is encoded by its own call. wdym and SentencePiece Lite are measured with and without their 1 MB word cache, which starts empty on each pass. Hugging Face tokenizers always uses its built-in cache of 10,000 words, which stays filled between passes.
- With 32 threads, all the lines are encoded as one batch of token IDs. wdym and SentencePiece Lite have no batch call, so the benchmark splits the lines evenly between its own threads, measured with and without one word cache per thread. Hugging Face tokenizers uses `encode_batch_fast`.

## Results

Measured on 2026-11-16 (UTC):

| | |
|---|---|
| CPU | AMD Ryzen 9 5950X (16 cores, 32 hardware threads); runs with one thread are pinned to one core, runs with 32 threads aren't pinned |
| Memory | 31 GB |
| OS | Arch Linux, kernel 7.2.14, `powersave` frequency governor with boost enabled |
| Container | Fedora 44, GCC 16.2.1, Rust 1.99.1 |
| Versions | wdym 0.1.1 (commit `90c90f3`), SentencePiece and SentencePiece Lite at commit `a28cbf0`, Hugging Face tokenizers 0.23.2 |

Across repeated full runs, speeds differed by up to 9% with one thread and up to 21% with 32 threads.

### Encoding from C++ and Rust (MB/s)

| Vocabulary size | Library | Threads | Without cache | With cache | Tokens |
|---|---|---|---|---|---|
| 32,000 | wdym | 1 | 76.2 | 87.1 | 1,051,396 |
| 32,000 | SentencePiece Lite | 1 | 49.1 | 56.1 | 1,051,673 |
| 32,000 | Hugging Face tokenizers | 1 | n/a | 9.2 | 1,051,490 |
| 32,000 | wdym | 32 | 926.4 | n/a | 1,051,396 |
| 32,000 | SentencePiece Lite | 32 | 609.6 | 611.5 | 1,051,673 |
| 32,000 | Hugging Face tokenizers | 32 | n/a | 77.8 | 1,051,490 |
| 128,000 | wdym | 1 | 106.2 | 111.3 | 941,699 |
| 128,000 | SentencePiece Lite | 1 | 60.5 | 66.2 | 941,995 |
| 128,000 | Hugging Face tokenizers | 1 | n/a | 7.7 | 941,793 |
| 128,000 | wdym | 32 | 1,096.1 | n/a | 941,699 |
| 128,000 | SentencePiece Lite | 32 | 786.3 | 716.4 | 941,995 |
| 128,000 | Hugging Face tokenizers | 32 | n/a | 72.0 | 941,793 |

### Training

| Vocabulary size | Library | Threads | Seconds | Peak memory (MB) |
|---|---|---|---|---|
| 32,000 | wdym | 1 | 1.0 | 205 |
| 32,000 | SentencePiece | 1 | 3.1 | 316 |
| 32,000 | Hugging Face tokenizers | 1 | 5.9 | 477 |
| 32,000 | SentencePiece | 32 | 1.3 | 466 |
| 32,000 | Hugging Face tokenizers | 32 | 4.3 | 737 |
| 128,000 | wdym | 1 | 1.2 | 219 |
| 128,000 | SentencePiece | 1 | 3.5 | 354 |
| 128,000 | Hugging Face tokenizers | 1 | 6.3 | 524 |
| 128,000 | SentencePiece | 32 | 1.7 | 499 |
| 128,000 | Hugging Face tokenizers | 32 | 6.7 | 782 |

### Limitations

- The results come from one machine. The CPU frequency wasn't fixed, which is one reason the results vary between runs.
- All libraries use their default compiler flags. Nothing is built with `-march=native`.
- The training set is 50 MB, so these numbers say little about training on much larger corpora.

## Running the benchmarks

The benchmarks run in a container image that pins every tool and library version. From the repository root, with Docker:

```sh
docker build -t wdym-benchmarks -f benchmarks/Dockerfile benchmarks
docker run --rm -v "$PWD":/workspace wdym-benchmarks benchmarks/run.sh
```

Podman works the same way with `podman` in place of `docker`. On systems with SELinux, add `:z` to the volume (`"$PWD":/workspace:z`).

`run.sh` builds wdym from the mounted repository, downloads the data on the first run, trains and encodes with every library, then writes to `benchmarks/results/`, which git ignores:

- `results.jsonl`, one line per measurement
- `results.md`, the tables above
- `environment.txt`, the hardware and versions

Environment variables adjust the run:

| Variable | Default | Meaning |
|---|---|---|
| `VOCABULARY_SIZES` | `32000 128000` | vocabulary sizes to train and encode with |
| `REPEATS` | `5` | encoding passes; the fastest one counts |
| `CPU` | `2` | CPU core the runs with one thread are pinned to |
| `THREADS` | every hardware thread | thread count of the multi-threaded runs |
| `JOBS` | `12` | parallel jobs when building wdym |

Pass them with `-e`, for example `docker run --rm -e VOCABULARY_SIZES=32000 -v "$PWD":/workspace wdym-benchmarks benchmarks/run.sh`.
