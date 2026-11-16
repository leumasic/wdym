import json
import sys

LIBRARY_NAMES = {
    "wdym": "wdym",
    "sentencepiece": "SentencePiece",
    "sentencepiece-lite": "SentencePiece Lite",
    "huggingface": "Hugging Face tokenizers",
}


def format_number(value, decimals=1):
    return "n/a" if value is None else f"{value:,.{decimals}f}"


with open(sys.argv[1]) as file:
    results = [json.loads(line) for line in file if line.strip()]

print("### Encoding from C++ and Rust (MB/s)\n")
print("| Vocabulary size | Library | Threads | Without cache | With cache | Tokens |")
print("|---|---|---|---|---|---|")
rows = {}
for result in results:
    if result["benchmark"] == "encode":
        row = rows.setdefault(
            (result["vocabulary_size"], result["library"], result["threads"]), {}
        )
        row["cache" if result["cache"] else "no_cache"] = result["megabytes_per_second"]
        row["tokens"] = result["tokens"]
for (vocabulary_size, library, threads), row in rows.items():
    print(
        f"| {vocabulary_size:,} | {LIBRARY_NAMES[library]} | {threads} | {format_number(row.get('no_cache'))} "
        f"| {format_number(row.get('cache'))} | {row['tokens']:,} |"
    )

print("\n### Training\n")
print("| Vocabulary size | Library | Threads | Seconds | Peak memory (MB) |")
print("|---|---|---|---|---|")
for result in results:
    if result["benchmark"] == "train":
        print(
            f"| {result['vocabulary_size']:,} | {LIBRARY_NAMES[result['library']]} | {result['threads']} "
            f"| {format_number(result['seconds'])} | {format_number(result['peak_memory_megabytes'], 0)} |"
        )
