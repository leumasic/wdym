#!/usr/bin/env bash
set -euo pipefail

benchmarks_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_directory="$(dirname "$benchmarks_directory")"
data_directory="$benchmarks_directory/data"
models_directory="$benchmarks_directory/models"
results_directory="$benchmarks_directory/results"
build_directory=/tmp/wdym-benchmarks-build
sentencepiece_directory=/opt/sentencepiece/build/src
lite_directory=/opt/sentencepiece/build/lite
huggingface_directory=/opt/huggingface_benchmarks/target/release

read -ra vocabulary_sizes <<< "${VOCABULARY_SIZES:-32000 128000}"
repeats="${REPEATS:-5}"
cpu="${CPU:-2}"
threads="${THREADS:-$(nproc)}"
export CMAKE_BUILD_PARALLEL_LEVEL="${JOBS:-12}"

mkdir -p "$models_directory" "$results_directory"
results="$results_directory/results.jsonl"
: > "$results"
peak_memory_kilobytes_file="$(mktemp)"

measure_training() {
  local library="$1" vocabulary_size="$2" thread_count="$3"
  shift 3

  local start_nanoseconds end_nanoseconds
  start_nanoseconds="$(date +%s%N)"
  /usr/bin/time --format %M --output "$peak_memory_kilobytes_file" "$@"
  end_nanoseconds="$(date +%s%N)"

  awk -v library="$library" -v vocabulary_size="$vocabulary_size" -v threads="$thread_count" \
    -v nanoseconds="$((end_nanoseconds - start_nanoseconds))" \
    -v kilobytes="$(cat "$peak_memory_kilobytes_file")" \
    'BEGIN {
      printf "{\"benchmark\": \"train\", \"library\": \"%s\", \"vocabulary_size\": %d, \"threads\": %d, \"seconds\": %f, \"peak_memory_megabytes\": %f}\n",
        library, vocabulary_size, threads, nanoseconds / 1e9, kilobytes / 1024
    }' | tee -a "$results"
}

cmake -S "$benchmarks_directory/cpp" -B "$build_directory" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_directory" --target train_wdym encode_wdym

if [[ ! -f "$data_directory/train.txt" ]]; then
  python3 "$benchmarks_directory/fetch_data.py" "$data_directory"
fi

for vocabulary_size in "${vocabulary_sizes[@]}"; do
  wdym_tokenizer="$models_directory/wdym-$vocabulary_size.wdym"
  sentencepiece_model_prefix="$models_directory/sentencepiece-$vocabulary_size"
  huggingface_tokenizer="$models_directory/huggingface-$vocabulary_size.json"

  for thread_count in 1 "$threads"; do
    pin=()
    if [[ "$thread_count" == 1 ]]; then
      pin=(taskset -c "$cpu")
      measure_training wdym "$vocabulary_size" "$thread_count" "${pin[@]}" "$build_directory/train_wdym" \
        "$data_directory/train.txt" "$vocabulary_size" "$wdym_tokenizer"
    fi

    measure_training sentencepiece "$vocabulary_size" "$thread_count" "${pin[@]}" "$sentencepiece_directory/spm_train" \
      --input="$data_directory/train.txt" \
      --model_prefix="$sentencepiece_model_prefix" \
      --model_type=bpe \
      --vocab_size="$vocabulary_size" \
      --normalization_rule_name=identity \
      --character_coverage=1.0 \
      --byte_fallback=false \
      --split_by_unicode_script=false \
      --split_by_number=false \
      --max_sentencepiece_length=512 \
      --max_sentence_length=1048576 \
      --input_sentence_size=0 \
      --bos_id=-1 \
      --eos_id=-1 \
      --num_threads="$thread_count" \
      --quiet
    measure_training huggingface "$vocabulary_size" "$thread_count" "${pin[@]}" "$huggingface_directory/train_huggingface" \
      "$data_directory/train.txt" "$vocabulary_size" "$huggingface_tokenizer" "$thread_count"

    "$lite_directory/spm_to_fb" \
      --model "$sentencepiece_model_prefix.model" \
      --output "$sentencepiece_model_prefix.spm.fb"

    "${pin[@]}" "$build_directory/encode_wdym" "$wdym_tokenizer" \
      "$data_directory/test.txt" "$vocabulary_size" "$repeats" "$thread_count" | tee -a "$results"
    "${pin[@]}" "$lite_directory/encode_lite" "$sentencepiece_model_prefix.spm.fb" \
      "$data_directory/test.txt" "$vocabulary_size" "$repeats" "$thread_count" | tee -a "$results"
    "${pin[@]}" "$huggingface_directory/encode_huggingface" "$huggingface_tokenizer" \
      "$data_directory/test.txt" "$vocabulary_size" "$repeats" "$thread_count" | tee -a "$results"
  done
done

{
  echo "date: $(date -u +%F)"
  echo "cpu: $(lscpu | sed -n 's/^Model name: *//p')"
  echo "memory: $(awk '/^MemTotal:/ {printf "%.0f GB", $2 / 1048576}' /proc/meminfo)"
  echo "kernel: $(uname -r)"
  echo "cpu frequency governor: $(cat "/sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor" 2>/dev/null || echo unknown)"
  echo "pinned to cpu: $cpu"
  echo "threads: $threads"
  echo "compiler: $(g++ --version | head -1)"
  echo "rust: $(rustc --version)"
  echo "hugging face tokenizers: $(sed -n 's/^tokenizers = "=\(.*\)"$/\1/p' /opt/huggingface_benchmarks/Cargo.toml)"
  echo "wdym commit: $(git -C "$repository_directory" rev-parse --short HEAD)$(git -C "$repository_directory" diff --quiet HEAD || echo ' with uncommitted changes')"
  echo "sentencepiece commit: $(git -C /opt/sentencepiece rev-parse --short HEAD)"
  echo "repeats: $repeats"
} > "$results_directory/environment.txt"

python3 "$benchmarks_directory/summarize.py" "$results" > "$results_directory/results.md"
cat "$results_directory/environment.txt" "$results_directory/results.md"
