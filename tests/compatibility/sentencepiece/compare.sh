#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <tokenizer.model> <text file>..." >&2
  exit 2
fi

compatibility_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_directory="${BUILD_DIRECTORY:-/tmp/wdym-sentencepiece-compatibility-build}"
sentencepiece_directory="${SENTENCEPIECE_DIRECTORY:-/opt/sentencepiece/build/src}"
export CMAKE_BUILD_PARALLEL_LEVEL="${JOBS:-12}"

model="$1"
shift

cmake -S "$compatibility_directory" -B "$build_directory" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$build_directory" --target compare_wdym > /dev/null

comparison_directory="$(mktemp -d)"
trap 'rm -rf "$comparison_directory"' EXIT

cat "$@" > "$comparison_directory/texts.txt"
"$sentencepiece_directory/spm_encode" --model="$model" --output_format=id \
  < "$comparison_directory/texts.txt" > "$comparison_directory/token_ids.txt"
"$sentencepiece_directory/spm_decode" --model="$model" --input_format=id \
  < "$comparison_directory/token_ids.txt" > "$comparison_directory/decoded.txt"

"$build_directory/compare_wdym" "$model" "$comparison_directory/texts.txt" \
  "$comparison_directory/token_ids.txt" "$comparison_directory/decoded.txt"
