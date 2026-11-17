import std;
import wdym;

using Clock = std::chrono::steady_clock;

struct TokenIdBatch {
  std::vector<std::uint32_t> tokenIds;
  std::vector<std::size_t> tokenIdOffsets;
};

std::vector<std::string> readLines(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);)
    lines.push_back(line);
  return lines;
}

TokenIdBatch encodeBatch(
    const wdym::BytePairEncoding& tokenizer,
    std::span<const std::string> texts,
    std::size_t threadCount,
    bool useCache
) {
  TokenIdBatch batch{.tokenIds = {}, .tokenIdOffsets = std::vector<std::size_t>(texts.size() + 1)};
  std::vector<std::vector<std::uint32_t>> chunkTokenIds(threadCount);

  const auto encodeChunk = [&](std::size_t chunkIndex) {
    std::vector<std::uint32_t> tokenIds;
    std::optional<wdym::BytePairEncoding::WordCache> cache;
    if (useCache)
      cache.emplace();

    const std::size_t firstTextIndex = texts.size() * chunkIndex / threadCount;
    const std::size_t endTextIndex = texts.size() * (chunkIndex + 1) / threadCount;

    for (std::size_t textIndex = firstTextIndex; textIndex < endTextIndex; ++textIndex) {
      const std::size_t tokenIdCountBeforeText = tokenIds.size();
      if (useCache)
        tokenizer.encode(texts[textIndex], tokenIds, *cache);
      else
        tokenizer.encode(texts[textIndex], tokenIds);
      batch.tokenIdOffsets[textIndex + 1] = tokenIds.size() - tokenIdCountBeforeText;
    }

    chunkTokenIds[chunkIndex] = std::move(tokenIds);
  };

  {
    std::vector<std::jthread> threads;
    for (std::size_t chunkIndex = 1; chunkIndex < threadCount; ++chunkIndex)
      threads.emplace_back(encodeChunk, chunkIndex);

    encodeChunk(0);
  }

  std::partial_sum(
      batch.tokenIdOffsets.begin(), batch.tokenIdOffsets.end(), batch.tokenIdOffsets.begin()
  );

  batch.tokenIds.reserve(batch.tokenIdOffsets.back());
  for (const std::vector<std::uint32_t>& tokenIds : chunkTokenIds)
    batch.tokenIds.append_range(tokenIds);

  return batch;
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

  const auto benchmarkEncoding = [&](bool useCache) {
    double bestSeconds = std::numeric_limits<double>::infinity();
    std::size_t tokenCount = 0;
    std::vector<std::uint32_t> tokenIds;

    for (int repeat = 0; repeat < repeats; ++repeat) {
      if (threadCount > 1) {
        const auto start = Clock::now();
        const TokenIdBatch batch = encodeBatch(tokenizer, lines, threadCount, useCache);
        bestSeconds = std::min(bestSeconds, std::chrono::duration<double>(Clock::now() - start).count());
        tokenCount = batch.tokenIds.size();
      } else {
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
    }

    printResult(useCache, tokenCount, bestSeconds);
  };

  benchmarkEncoding(false);
  benchmarkEncoding(true);
}
