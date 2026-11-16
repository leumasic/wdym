use std::time::Instant;

use tokenizers::Tokenizer;

fn main() {
    let arguments: Vec<String> = std::env::args().collect();
    if arguments.len() != 6 {
        eprintln!(
            "usage: encode_huggingface <tokenizer.json> <test file> <vocabulary size> <repeats> <threads>"
        );
        std::process::exit(1);
    }

    let tokenizer = Tokenizer::from_file(&arguments[1]).unwrap();
    let text = std::fs::read_to_string(&arguments[2]).unwrap();
    let lines: Vec<&str> = text.strip_suffix('\n').unwrap_or(&text).split('\n').collect();
    let vocabulary_size: usize = arguments[3].parse().unwrap();
    let repeats: usize = arguments[4].parse().unwrap();
    let thread_count: usize = arguments[5].parse().unwrap();

    tokenizers::parallelism::set_parallelism(thread_count > 1);
    rayon::ThreadPoolBuilder::new()
        .num_threads(thread_count)
        .build_global()
        .unwrap();

    let byte_count: usize = lines.iter().map(|line| line.len()).sum();
    let mut best_seconds = f64::INFINITY;
    let mut token_count: usize = 0;

    for _ in 0..repeats {
        let batch = lines.clone();

        let start = Instant::now();
        token_count = if thread_count > 1 {
            let encodings = tokenizer.encode_batch_fast(batch, false).unwrap();
            encodings.iter().map(|encoding| encoding.len()).sum()
        } else {
            let mut line_token_count = 0;
            for line in batch {
                line_token_count += tokenizer.encode_fast(line, false).unwrap().len();
            }
            line_token_count
        };
        best_seconds = best_seconds.min(start.elapsed().as_secs_f64());
    }

    println!(
        r#"{{"benchmark": "encode", "api": "rust", "library": "huggingface", "vocabulary_size": {}, "threads": {}, "cache": true, "tokens": {}, "megabytes_per_second": {}}}"#,
        vocabulary_size,
        thread_count,
        token_count,
        byte_count as f64 / best_seconds / 1e6
    );
}
