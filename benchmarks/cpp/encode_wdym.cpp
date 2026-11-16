import std;
import wdym;

using Clock = std::chrono::steady_clock;

std::vector<std::string> readLines(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);)
    lines.push_back(line);
  return lines;
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::println(
        std::cerr, "usage: encode_wdym <tokenizer> <test file> <vocabulary size> <repeats> <threads>"
    );
    return 1;
  }

  const auto tokenizer = wdym::BytePairEncoding::load(argv[1]);
  const std::vector<std::string> lines = readLines(argv[2]);
  const int vocabularySize = std::stoi(argv[3]);
  const int repeats = std::stoi(argv[4]);
  const std::size_t threadCount = std::stoul(argv[5]);

  std::size_t byteCount = 0;
  for (const std::string& line : lines)
    byteCount += line.size();

  const auto printResult = [&](bool useCache, std::size_t tokenCount, double bestSeconds) {
    std::println(
        R"({{"benchmark": "encode", "api": "c++", "library": "wdym", "vocabulary_size": {}, "threads": {}, "cache": {}, "tokens": {}, "megabytes_per_second": {}}})",
        vocabularySize,
        threadCount,
        useCache,
        tokenCount,
        byteCount / bestSeconds / 1e6
    );
  };

  if (threadCount > 1) {
    const std::vector<std::string_view> texts(std::from_range, lines);
    double bestSeconds = std::numeric_limits<double>::infinity();
    std::size_t tokenCount = 0;

    for (int repeat = 0; repeat < repeats; ++repeat) {
      const auto start = Clock::now();
      const wdym::TokenIdBatch batch = tokenizer.encodeBatch(texts, threadCount);
      bestSeconds = std::min(bestSeconds, std::chrono::duration<double>(Clock::now() - start).count());
      tokenCount = batch.tokenIds.size();
    }

    printResult(false, tokenCount, bestSeconds);
    return 0;
  }

  const auto benchmarkLineByLineEncoding = [&](bool useCache) {
    double bestSeconds = std::numeric_limits<double>::infinity();
    std::size_t tokenCount = 0;
    std::vector<std::uint32_t> tokenIds;

    for (int repeat = 0; repeat < repeats; ++repeat) {
      wdym::BytePairEncoding::WordCache cache;
      tokenCount = 0;

      const auto start = Clock::now();
      for (const std::string& line : lines) {
        tokenIds.clear();
        if (useCache)
          tokenizer.encode(line, tokenIds, cache);
        else
          tokenizer.encode(line, tokenIds);
        tokenCount += tokenIds.size();
      }
      bestSeconds = std::min(bestSeconds, std::chrono::duration<double>(Clock::now() - start).count());
    }

    printResult(useCache, tokenCount, bestSeconds);
  };

  benchmarkLineByLineEncoding(false);
  benchmarkLineByLineEncoding(true);
}
