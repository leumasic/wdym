use tokenizers::Tokenizer;
use tokenizers::models::TrainerWrapper;
use tokenizers::models::bpe::{BPE, BpeTrainer};
use tokenizers::pre_tokenizers::metaspace::{Metaspace, PrependScheme};

fn main() {
    let arguments: Vec<String> = std::env::args().collect();
    if arguments.len() != 5 {
        eprintln!(
            "usage: train_huggingface <train file> <vocabulary size> <tokenizer.json> <threads>"
        );
        std::process::exit(1);
    }

    let text = std::fs::read_to_string(&arguments[1]).unwrap();
    let lines = text.strip_suffix('\n').unwrap_or(&text).split('\n');
    let vocabulary_size: usize = arguments[2].parse().unwrap();
    let thread_count: usize = arguments[4].parse().unwrap();

    tokenizers::parallelism::set_parallelism(thread_count > 1);
    rayon::ThreadPoolBuilder::new()
        .num_threads(thread_count)
        .build_global()
        .unwrap();

    let mut tokenizer = Tokenizer::new(BPE::default());
    tokenizer.with_pre_tokenizer(Some(Metaspace::new('▁', PrependScheme::Always, true)));

    let mut trainer: TrainerWrapper = BpeTrainer::builder()
        .vocab_size(vocabulary_size)
        .show_progress(false)
        .build()
        .into();

    tokenizer.train(&mut trainer, lines).unwrap();
    tokenizer.save(&arguments[3], true).unwrap();
}
